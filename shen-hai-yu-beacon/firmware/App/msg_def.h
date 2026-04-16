#ifndef __MSG_DEF_H__
#define __MSG_DEF_H__

#include <rtthread.h>

/* ---- GNSS 消息（mq_gnss） ---- */
typedef struct {
    rt_bool_t  valid;
    float      latitude;
    float      longitude;
    float      altitude;
    rt_uint8_t satellites;
    char       utc_time[12];
} MsgGnss_t;

/* ---- 短报文发送消息（mq_rdss_tx） ---- */
#define RDSS_MSG_BODY_LEN   78

typedef enum {
    RDSS_MSG_POSITION = 0,
    RDSS_MSG_SOS,
} RdssMsgType_t;

typedef struct {
    RdssMsgType_t type;
    char          dest_id[12];
    char          body[RDSS_MSG_BODY_LEN];
} MsgRdssTx_t;

/* ---- 运行阶段（按激活后经过时间划分） ---- */
typedef enum {
    PHASE_IDLE    = 0,  /* 休眠待机，等待长按 SOS */
    PHASE_0_1H,         /* 0~1h:  每 2min 上报一次 */
    PHASE_1_3H,         /* 1~3h:  每 5min 上报一次 */
    PHASE_3_72H,        /* 3~72h: 每15min 上报一次 */
    PHASE_EXPIRED,      /* 72h 后停止 */
    PHASE_TEST,         /* 超长按8s：测试模式，每2min发送一次，任意按键退出 */
} BeaconPhase_t;

/* ---- 设备全局状态（受 mtx_status 保护） ---- */
typedef struct {
    BeaconPhase_t phase;
    rt_bool_t     sos_active;       /* 救援模式已激活 */
    rt_uint32_t   sos_start_sec;    /* 激活时刻（秒，相对系统tick） */
    rt_uint32_t   total_reports;    /* 累计发送次数 */
    rt_bool_t     gnss_valid;
    float         latitude;
    float         longitude;
    rt_uint16_t   bat_mv;           /* 电池电压 mV */
    rt_uint8_t    bat_pct;          /* 电量百分比 0~100 */
    rt_bool_t     usb_in;
} DeviceStatus_t;

extern DeviceStatus_t g_status;

/* ---- IPC 对象（main.c 定义） ---- */
extern rt_mq_t    mq_gnss;
extern rt_mq_t    mq_rdss_tx;
extern rt_sem_t   sem_sos;          /* 长按 SOS 触发 */
extern rt_sem_t   sem_bat_display;  /* 短按 SOS 触发 */
extern rt_sem_t   sem_work_done;    /* 单次工作周期完成 */
extern rt_mutex_t mtx_status;

/* ---- LED 触发接口（thread_led.c 实现） ---- */
void LED_TriggerShortPress(void);  /* 短按：电量显示5s */
void LED_TriggerLongPress(void);   /* 长按：全闪5s */

#endif /* __MSG_DEF_H__ */
