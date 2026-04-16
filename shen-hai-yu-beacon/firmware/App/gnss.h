#ifndef __GNSS_H__
#define __GNSS_H__

#include "system.h"

typedef struct {
    bool    valid;          /* 定位是否有效 */
    float   latitude;       /* 纬度 (度，正北) */
    float   longitude;      /* 经度 (度，正东) */
    float   altitude;       /* 海拔 (m) */
    float   speed;          /* 速度 (knots) */
    uint8_t satellites;     /* 使用卫星数 */
    uint8_t fix_quality;    /* 定位质量 0=无效 1=GPS 4=北斗 */
    char    utc_time[12];   /* UTC时间 hhmmss.ss */
    char    utc_date[8];    /* UTC日期 ddmmyy */
    uint32_t timestamp;     /* 系统时间戳 (ms) */
} GnssData_t;

void         GNSS_Init(void);
void         GNSS_PowerOn(void);
void         GNSS_PowerOff(void);
void         GNSS_Process(void);    /* 在主循环中调用 */
bool         GNSS_IsValid(void);
GnssData_t  *GNSS_GetData(void);

/* 格式化坐标字符串，用于短报文发送 */
void GNSS_FormatPosition(char *buf, uint16_t len);

#endif /* __GNSS_H__ */
