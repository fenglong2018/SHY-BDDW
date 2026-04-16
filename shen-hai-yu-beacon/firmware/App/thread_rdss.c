/**
 * @file  thread_rdss.c
 * @brief RDSS 短报文线程 — TD3050/TD3203B ASCII 协议
 *
 * 协议参考：TD3050 接口协议
 * 所有语句末尾加 \r\n
 *
 * 发送报文：$CCTCQ,收信方ID,频点,入站确认,编码类别,数据,频度*hh\r\n
 *   - 频点 2=Lf1, 3=Lf2
 *   - 入站确认 1=不需要
 *   - 编码类别 2=代码(ASCII)
 *   - 频度 0=单次
 *
 * 响应：$BDFKI,时间,TCQ,Y/N,失败原因,剩余时间*hh
 *   Y=发射成功, N=失败
 *
 * 查询本机信息：$CCICR,0,00*68
 * 响应：$BDICP,...*hh
 *
 * 注入 GNSS 位置（无接收通路时必须）：$CCNPS,...*hh
 */

#include <rtthread.h>
#include <string.h>
#include <stdio.h>
#include "msg_def.h"
#include "debug_config.h"
#include "../RTThread/board.h"
#include "../Drivers/uart.h"
#include "../Drivers/gpio.h"
#include "../RTThread/rtconfig.h"

/* 前向声明：thread_config.c 提供 */
extern void Config_SetBdId(const char *id);

#define TAG  "RDSS"
#define EN   DBG_RDSS
#define RESP_BUF_SIZE   256
#define RESP_TIMEOUT_MS 8000   /* 短报文发射最长等待 8s */
#define FREQ_POINT      2      /* 使用 Lf1 频点 */

static char s_resp[RESP_BUF_SIZE];
static rt_uint16_t s_resp_idx = 0;

/* ------------------------------------------------------------------ */
/* NMEA 校验和计算：'$' 和 '*' 之间所有字符异或                        */
/* ------------------------------------------------------------------ */
static rt_uint8_t _nmea_cs(const char *sentence)
{
    rt_uint8_t cs = 0;
    const char *p = sentence;
    if (*p == '$') p++;
    for (; *p && *p != '*'; p++) cs ^= (rt_uint8_t)*p;
    return cs;
}

/* ------------------------------------------------------------------ */
/* 发送带校验和的 NMEA 语句                                             */
/* 格式：$<body>*<HH>\r\n                                              */
/* ------------------------------------------------------------------ */
static void _send_nmea(const char *body)
{
    char frame[RESP_BUF_SIZE];
    /* 计算校验和（body 不含 $ 和 *） */
    rt_uint8_t cs = 0;
    for (const char *p = body; *p; p++) cs ^= (rt_uint8_t)*p;
    rt_snprintf(frame, sizeof(frame), "$%s*%02X\r\n", body, cs);
    UART_SendStr(UART_CH2, frame);
    DDBG(TAG, EN, "TX: %s", frame);
}

/* ------------------------------------------------------------------ */
/* 等待包含 keyword 的响应行，超时返回 RT_FALSE                         */
/* ------------------------------------------------------------------ */
static rt_bool_t _wait_resp(const char *keyword, rt_uint32_t timeout_ms)
{
    rt_uint32_t start = rt_tick_get_millisecond();
    rt_uint8_t  byte;

    rt_memset(s_resp, 0, sizeof(s_resp));
    s_resp_idx = 0;

    while ((rt_tick_get_millisecond() - start) < timeout_ms) {
        if (UART_ReadByte(UART_CH2, &byte)) {
            if (s_resp_idx < RESP_BUF_SIZE - 1) {
                s_resp[s_resp_idx++] = (char)byte;
            }
            /* 收到完整行（\n 结尾）时检查 */
            if (byte == '\n') {
                DDBG(TAG, EN, "RX: %s", s_resp);
                if (strstr(s_resp, keyword)) return RT_TRUE;
                /* 清空，准备接收下一行 */
                rt_memset(s_resp, 0, sizeof(s_resp));
                s_resp_idx = 0;
            }
        }
        rt_thread_mdelay(1);
    }
    return RT_FALSE;
}

/* ------------------------------------------------------------------ */
/* 初始化：查询本机 IC 模块信息                                         */
/* ------------------------------------------------------------------ */
static rt_bool_t _rdss_init(void)
{
    rt_kprintf("[%s] power on\n", TAG);
    RDSS_POWER_ON();
    PA_POWER_ON();
    rt_thread_mdelay(3000);

    UART_SendStr(UART_CH2, "$CCICR,0,00*68\r\n");
    if (_wait_resp("$BDICP", 3000)) {
        DINF(TAG, EN, "IC info: %s", s_resp);
        /* 解析第一个字段（用户地址/卡号）*/
        char bdid[32] = "";
        const char *p = strchr(s_resp, ',');
        if (p) {
            p++;
            size_t i = 0;
            while (*p && *p != ',' && i < sizeof(bdid) - 1)
                bdid[i++] = *p++;
            bdid[i] = '\0';
            if (i > 0) Config_SetBdId(bdid);
        }
        return RT_TRUE;
    }
    DERR(TAG, EN, "init timeout, continue anyway");
    return RT_TRUE;  /* 即使查询超时也继续，模块可能正常工作 */
}

/* ------------------------------------------------------------------ */
/* 注入 GNSS 位置到 RDSS 模块（无接收通路时必须，误差 5 分钟内）        */
/* $CCNPS,周内秒,经度,E,纬度,N,高度,M*hh                               */
/* ------------------------------------------------------------------ */
static void _inject_position(float lat, float lon, float alt,
                             rt_uint32_t tow_sec)
{
    /* 转换为度分格式 ddmm.mmmm，正确处理负数 */
    char lat_dir = (lat >= 0) ? 'N' : 'S';
    char lon_dir = (lon >= 0) ? 'E' : 'W';
    float abs_lat = (lat >= 0) ? lat : -lat;
    float abs_lon = (lon >= 0) ? lon : -lon;

    int   lat_deg = (int)abs_lat;
    float lat_min = (abs_lat - lat_deg) * 60.0f;
    int   lon_deg = (int)abs_lon;
    float lon_min = (abs_lon - lon_deg) * 60.0f;

    char body[128];
    rt_snprintf(body, sizeof(body),
                "CCNPS,%lu,%02d%07.4f,%c,%02d%07.4f,%c,%.1f,M",
                (unsigned long)tow_sec,
                lon_deg, lon_min, lon_dir,
                lat_deg, lat_min, lat_dir,
                alt);
    _send_nmea(body);
    rt_thread_mdelay(200);
}

/* ------------------------------------------------------------------ */
/* 发送短报文                                                           */
/* $CCTCQ,收信方ID,频点,入站确认,编码类别,数据,频度*hh                  */
/* 响应 $BDFKI,...,Y,...  表示发射成功                                  */
/* ------------------------------------------------------------------ */
static rt_err_t _send_msg(const MsgRdssTx_t *msg)
{
    char body[RESP_BUF_SIZE];

    /* 先注入当前位置（有接收通路可省略，但保险起见都注入） */
    rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
    float lat = g_status.latitude;
    float lon = g_status.longitude;
    float alt = 0.0f;
    rt_mutex_release(mtx_status);

    /* 用系统 tick 估算周内秒（粗略，GNSS 定位后会有精确时间） */
    rt_uint32_t tow = (rt_tick_get_millisecond() / 1000UL) % 604800UL;
    _inject_position(lat, lon, alt, tow);

    /* 构造 CCTCQ 语句体（不含 $ 和 *hh） */
    /* 编码类别 2=代码(ASCII)，频度 0=单次 */
    rt_snprintf(body, sizeof(body),
                "CCTCQ,%s,%d,1,2,%s,0",
                msg->dest_id,
                FREQ_POINT,
                msg->body);
    _send_nmea(body);

    if (!_wait_resp("$BDFKI", RESP_TIMEOUT_MS)) {
        DERR(TAG, EN, "BDFKI timeout");
        return -RT_ETIMEOUT;
    }
    if (strstr(s_resp, ",Y,") || strstr(s_resp, ",Y*")) {
        DINF(TAG, EN, "send OK");
        return RT_EOK;
    }
    DERR(TAG, EN, "send FAIL: %s", s_resp);
    return -RT_ERROR;
}

/* ------------------------------------------------------------------ */
/* 主线程                                                               */
/* ------------------------------------------------------------------ */
static void thread_rdss_entry(void *param)
{
    MsgRdssTx_t msg;
    rt_uint8_t  retry = 0;

    rt_kprintf("[%s] thread started\n", TAG);
    while (!g_status.gnss_valid) rt_thread_mdelay(1000);
    _rdss_init();

    while (1) {
        if (rt_mq_recv(mq_rdss_tx, &msg, sizeof(msg),
                       RT_WAITING_FOREVER) == RT_EOK) {
            DINF(TAG, EN, "sending to %s: %s", msg.dest_id, msg.body);
            for (retry = 0; retry < 3; retry++) {
                if (_send_msg(&msg) == RT_EOK) break;
                DERR(TAG, EN, "retry %d/3", retry + 1);
                rt_thread_mdelay(3000);
            }
            if (retry >= 3) DERR(TAG, EN, "failed after 3 retries");
            rt_sem_release(sem_work_done);
        }
    }
}

static int rdss_thread_init(void)
{
    rt_thread_t tid = rt_thread_create(
        "rdss",
        thread_rdss_entry, RT_NULL,
        THREAD_STACK_RDSS,
        THREAD_PRIO_RDSS, 10
    );
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);
    return 0;
}
INIT_APP_EXPORT(rdss_thread_init);
