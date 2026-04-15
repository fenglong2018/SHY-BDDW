#ifndef __RDSS_H__
#define __RDSS_H__

#include "system.h"

#define RDSS_MSG_MAX_LEN    78   /* 北斗短报文最大字节数 */
#define RDSS_AT_TIMEOUT_MS  5000

typedef enum {
    RDSS_OK = 0,
    RDSS_ERR_TIMEOUT,
    RDSS_ERR_NO_SIM,
    RDSS_ERR_NO_SIGNAL,
    RDSS_ERR_SEND_FAIL,
} RdssErr_t;

typedef struct {
    bool     sim_present;   /* SIM 卡已插入 */
    bool     registered;    /* 已注册网络 */
    int8_t   rssi;          /* 信号强度 */
    char     module_id[20]; /* 模块 ID */
} RdssStatus_t;

void       RDSS_Init(void);
void       RDSS_PowerOn(void);
void       RDSS_PowerOff(void);
bool       RDSS_CheckSimCard(void);
RdssErr_t  RDSS_SendAT(const char *cmd, const char *expect, uint32_t timeout_ms);
RdssErr_t  RDSS_SendMessage(const char *dest_id, const char *msg);
RdssErr_t  RDSS_SendPosition(float lat, float lon);
void       RDSS_Process(void);
RdssStatus_t *RDSS_GetStatus(void);

#endif /* __RDSS_H__ */
