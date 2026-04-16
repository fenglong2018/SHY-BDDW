/**
 * @file  thread_gnss.c
 * @brief GNSS 线程 — 解析泰斗 SDBP 二进制协议，发布位置到 mq_gnss
 *
 * 协议参考：TD-SDBP V3.29
 * 调试开关：debug_config.json → modules.GNSS / options.GNSS_DUMP_RAW
 */

#include <rtthread.h>
#include <string.h>
#include <math.h>
#include "msg_def.h"
#include "debug_config.h"
#include "../RTThread/board.h"
#include "../Drivers/uart.h"
#include "../Drivers/gpio.h"
#include "../RTThread/rtconfig.h"

#define TAG  "GNSS"
/* 本模块调试使能 */
#define EN   DBG_GNSS

/* ---- SDBP 帧头 ---- */
#define SDBP_SYNC0   0x23U
#define SDBP_SYNC1   0x3EU

/* ---- 识别码 ---- */
#define SDBP_TYPE_CFG   0x03U
#define SDBP_TYPE_DAT   0x06U
#define SDBP_FUNC_SDBP  0x52U   /* CFG-SDBP */
#define SDBP_FUNC_LLA3  0x1DU   /* DAT-LLA3 */

/* ---- 接收状态机 ---- */
typedef enum {
    RX_SYNC0 = 0,
    RX_SYNC1,
    RX_TYPE,
    RX_FUNC,
    RX_LEN_L,
    RX_LEN_H,
    RX_DATA,
    RX_CS_L,
    RX_CS_H,
} RxState_t;

#define SDBP_MAX_DATA  256U

static RxState_t s_state   = RX_SYNC0;
static rt_uint8_t  s_type  = 0;
static rt_uint8_t  s_func  = 0;
static rt_uint16_t s_len   = 0;
static rt_uint16_t s_idx   = 0;
static rt_uint8_t  s_data[SDBP_MAX_DATA];
static rt_uint16_t s_cs_recv = 0;

/* ---- Fletcher-16 校验 ---- */
static rt_uint16_t _fletcher16(const rt_uint8_t *buf, rt_uint16_t len)
{
    rt_uint16_t cs1 = 0, cs2 = 0;
    while (len--) {
        cs1 += *buf++;
        cs2 += cs1;
    }
    return (rt_uint16_t)((cs2 << 8) | (cs1 & 0xFF));
}

/* ---- 构造并发送 SDBP 帧 ---- */
static void _sdbp_send(rt_uint8_t type, rt_uint8_t func,
                       const rt_uint8_t *data, rt_uint16_t len)
{
    /* 校验和范围：识别码 + 数据长度 + 数据 */
    rt_uint8_t cs_buf[4 + SDBP_MAX_DATA];
    cs_buf[0] = type;
    cs_buf[1] = func;
    cs_buf[2] = (rt_uint8_t)(len & 0xFF);
    cs_buf[3] = (rt_uint8_t)(len >> 8);
    if (data && len) rt_memcpy(&cs_buf[4], data, len);
    rt_uint16_t cs = _fletcher16(cs_buf, 4 + len);

    /* 发送帧 */
    UART_SendByte(UART_CH1, SDBP_SYNC0);
    UART_SendByte(UART_CH1, SDBP_SYNC1);
    UART_SendByte(UART_CH1, type);
    UART_SendByte(UART_CH1, func);
    UART_SendByte(UART_CH1, (rt_uint8_t)(len & 0xFF));
    UART_SendByte(UART_CH1, (rt_uint8_t)(len >> 8));
    if (data && len) UART_SendBuf(UART_CH1, data, len);
    UART_SendByte(UART_CH1, (rt_uint8_t)(cs & 0xFF));
    UART_SendByte(UART_CH1, (rt_uint8_t)(cs >> 8));
}

/* ---- 打开 LLA3 在 UART1 上的主动输出（每次定位输出一次）---- */
/* SDBP-CFG-SDBP-I2: 类型号=0x06, 语句ID=0x1D, 端口=1(UART1), 频率=1, 保留=0 */
static void _enable_lla3_output(void)
{
    rt_uint8_t payload[5] = { 0x06, 0x1D, 0x01, 0x01, 0x00 };
    _sdbp_send(SDBP_TYPE_CFG, SDBP_FUNC_SDBP, payload, sizeof(payload));
    rt_thread_mdelay(200);  /* 等待模块处理 */
    rt_kprintf("[%s] LLA3 output enabled\n", TAG);
}

static void _set_dynamic_model(void)
{
    rt_uint8_t payload[2] = { 0x05, 0x00 };
    _sdbp_send(SDBP_TYPE_CFG, 0x31, payload, sizeof(payload));
    rt_thread_mdelay(200);
    rt_kprintf("[%s] dynamic model: marine\n", TAG);
}

/* ---- 解析 LLA3 数据帧 ---- */
/*
 * LLA3 (0x06/0x1D) 数据结构（64字节）：
 * [0-3]   I32U 本地时标
 * [4]     I8U  参考坐标系
 * [5]     B8   有效标识 (BIT0~3=位置类型, BIT4~7=定位标识)
 * [6]     I8U  跟踪卫星数
 * [7]     I8U  定位卫星数
 * [8-9]   I16U UTC年
 * [10]    I8U  UTC月
 * [11]    I8U  UTC日
 * [12]    I8U  UTC时
 * [13]    I8U  UTC分
 * [14-15] I16U UTC秒(ms)
 * [16-23] F64  经度(deg)
 * [24-31] F64  纬度(deg)
 * [32-35] F32  高度(m)
 * [36-39] F32  大地水准面修正(m)
 * [40-43] I32U 水平位置精度(10^-3 m)
 * [44-47] I32U 垂直位置精度(10^-3 m)
 * [48-51] F32  地速(m/s)
 * [52-55] I32U 水平速度精度
 * [56-59] F32  方向(deg)
 * [60-63] I32U 方向精度
 */
static void _parse_lla3(const rt_uint8_t *d, rt_uint16_t len)
{
    if (len < 64) return;

    rt_uint8_t valid_flag = d[5];
    rt_uint8_t pos_type   = valid_flag & 0x0F;  /* BIT0~3 */
    rt_uint8_t fix_flag   = (valid_flag >> 4) & 0x0F; /* BIT4~7 */

    /* 位置类型 2=2D有效, 3=3D有效；定位标识 1=公开模式有效 */
    if (pos_type < 2 || fix_flag == 0) {
        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        g_status.gnss_valid = RT_FALSE;
        rt_mutex_release(mtx_status);
        return;
    }

    /* 读取 F64 经纬度（小端） */
    double lon, lat;
    rt_memcpy(&lon, &d[16], 8);
    rt_memcpy(&lat, &d[24], 8);

    float alt;
    rt_memcpy(&alt, &d[32], 4);

    rt_uint8_t sat_used = d[7];

    /* UTC 时间 */
    rt_uint16_t utc_year  = (rt_uint16_t)(d[8] | (d[9] << 8));
    rt_uint8_t  utc_month = d[10];
    rt_uint8_t  utc_day   = d[11];
    rt_uint8_t  utc_hour  = d[12];
    rt_uint8_t  utc_min   = d[13];
    rt_uint16_t utc_sec_ms= (rt_uint16_t)(d[14] | (d[15] << 8));

    MsgGnss_t msg;
    msg.valid      = RT_TRUE;
    msg.latitude   = (float)lat;
    msg.longitude  = (float)lon;
    msg.altitude   = alt;
    msg.satellites = sat_used;
    rt_snprintf(msg.utc_time, sizeof(msg.utc_time), "%02d%02d%05.2f",
                utc_hour, utc_min, utc_sec_ms / 1000.0f);

    rt_mq_send(mq_gnss, &msg, sizeof(msg));

    rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
    g_status.gnss_valid = RT_TRUE;
    g_status.latitude   = (float)lat;
    g_status.longitude  = (float)lon;
    rt_mutex_release(mtx_status);

    DINF(TAG, EN, "fix: %.5f, %.5f, alt=%.1f, sat=%d, %04d-%02d-%02d %02d:%02d:%05.2f",
               lat, lon, alt, sat_used,
               utc_year, utc_month, utc_day,
               utc_hour, utc_min, utc_sec_ms / 1000.0f);

#if DBG_GNSS_DUMP_RAW
    /* 原始帧 hex dump */
    rt_kprintf("[GNSS] RAW[%d]: ", len);
    for (rt_uint16_t i = 0; i < len && i < 32; i++)
        rt_kprintf("%02X ", d[i]);
    rt_kprintf("\n");
#endif
}

/* ---- 处理完整帧 ---- */
static void _process_frame(void)
{
    /* 验证校验和 */
    rt_uint8_t cs_buf[4 + SDBP_MAX_DATA];
    cs_buf[0] = s_type;
    cs_buf[1] = s_func;
    cs_buf[2] = (rt_uint8_t)(s_len & 0xFF);
    cs_buf[3] = (rt_uint8_t)(s_len >> 8);
    rt_memcpy(&cs_buf[4], s_data, s_len);
    rt_uint16_t cs_calc = _fletcher16(cs_buf, 4 + s_len);

    if (cs_calc != s_cs_recv) {
        DERR(TAG, EN, "checksum error: calc=0x%04X recv=0x%04X", cs_calc, s_cs_recv);
        return;
    }

    /* 只处理 DAT-LLA3 */
    if (s_type == SDBP_TYPE_DAT && s_func == SDBP_FUNC_LLA3) {
        _parse_lla3(s_data, s_len);
    }
}

/* ---- 字节级状态机 ---- */
static void _feed_byte(rt_uint8_t byte)
{
    switch (s_state) {
    case RX_SYNC0:
        if (byte == SDBP_SYNC0) s_state = RX_SYNC1;
        break;
    case RX_SYNC1:
        s_state = (byte == SDBP_SYNC1) ? RX_TYPE : RX_SYNC0;
        break;
    case RX_TYPE:
        s_type  = byte;
        s_state = RX_FUNC;
        break;
    case RX_FUNC:
        s_func  = byte;
        s_state = RX_LEN_L;
        break;
    case RX_LEN_L:
        s_len   = byte;
        s_state = RX_LEN_H;
        break;
    case RX_LEN_H:
        s_len  |= (rt_uint16_t)(byte << 8);
        s_idx   = 0;
        if (s_len > SDBP_MAX_DATA) {
            rt_kprintf("[%s] frame too long: %d, reset\n", TAG, s_len);
            /* 清空缓冲，重新同步 */
            s_idx = 0;
            rt_memset(s_data, 0, sizeof(s_data));
            s_state = RX_SYNC0;
        } else if (s_len == 0) {
            s_state = RX_CS_L;
        } else {
            s_state = RX_DATA;
        }
        break;
    case RX_DATA:
        s_data[s_idx++] = byte;
        if (s_idx >= s_len) s_state = RX_CS_L;
        break;
    case RX_CS_L:
        s_cs_recv = byte;
        s_state   = RX_CS_H;
        break;
    case RX_CS_H:
        s_cs_recv |= (rt_uint16_t)(byte << 8);
        _process_frame();
        s_state = RX_SYNC0;
        break;
    }
}

/* ---- 主线程 ---- */
static void thread_gnss_entry(void *param)
{
    rt_uint8_t byte;

    rt_kprintf("[%s] thread started\n", TAG);
    GNSS_POWER_ON();
    rt_thread_mdelay(1000);  /* 等待模块启动 */

    /* 配置：打开 LLA3 输出 + 船载运动模型 */
    _enable_lla3_output();
    rt_thread_mdelay(100);
    _set_dynamic_model();

    while (1) {
        while (UART_ReadByte(UART_CH1, &byte)) {
            _feed_byte(byte);
        }
        rt_thread_mdelay(5);
    }
}

static int gnss_thread_init(void)
{
    rt_thread_t tid = rt_thread_create(
        "gnss",
        thread_gnss_entry, RT_NULL,
        THREAD_STACK_GNSS,
        THREAD_PRIO_GNSS, 10
    );
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);
    return 0;
}
INIT_APP_EXPORT(gnss_thread_init);
