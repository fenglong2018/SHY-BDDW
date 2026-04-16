#ifndef __BEACON_H__
#define __BEACON_H__

#include "system.h"

/* 工作状态机 */
typedef enum {
    BEACON_STATE_IDLE = 0,  /* 待机 */
    BEACON_STATE_INIT,      /* 初始化 */
    BEACON_STATE_LOCATING,  /* 定位中 */
    BEACON_STATE_REPORTING, /* 上报中 */
    BEACON_STATE_SOS,       /* SOS 紧急模式 */
    BEACON_STATE_CHARGING,  /* 充电中 */
    BEACON_STATE_LOW_BAT,   /* 低电量 */
} BeaconState_t;

/* 配置参数 */
typedef struct {
    uint32_t report_interval_ms;    /* 正常上报间隔 (ms)，默认 300000 = 5min */
    uint32_t sos_interval_ms;       /* SOS 上报间隔 (ms)，默认 30000 = 30s */
    uint8_t  low_bat_threshold;     /* 低电量阈值 (%)，默认 15 */
    char     rescue_center_id[20];  /* 救援中心短报文 ID */
} BeaconConfig_t;

void          Beacon_Init(void);
void          Beacon_Run(void);         /* 主循环调用 */
void          Beacon_TriggerSOS(void);  /* 触发 SOS */
BeaconState_t Beacon_GetState(void);
BeaconConfig_t *Beacon_GetConfig(void);

#endif /* __BEACON_H__ */
