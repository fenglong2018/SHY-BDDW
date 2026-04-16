#ifndef __ADC_H__
#define __ADC_H__

#include "system.h"  /* 包含 stdint.h / stdbool.h */

void     ADC_Init(void);
uint16_t ADC_Read(uint8_t channel);
uint16_t ADC_ReadBatMv(void);
uint8_t  ADC_BatPercent(void);                          /* 放电状态电量 */
uint8_t  ADC_BatPercentEx(uint16_t mv, bool charging);  /* 带充电补偿 */

/* 低电量阈值 */
#define BAT_LOW_THRESHOLD_PCT   10U    /* 10% 以下低电量告警 */
#define BAT_CRIT_THRESHOLD_MV   3400U  /* 3.4V 以下停止发报文 */

#endif /* __ADC_H__ */
