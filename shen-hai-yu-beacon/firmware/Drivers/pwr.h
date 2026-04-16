#ifndef __PWR_H__
#define __PWR_H__

#include "system.h"  /* 包含 stdint.h */

void PWR_Init(void);
void PWR_EnterStop(void);
void PWR_EnterStopSeconds(uint32_t sleep_sec);
void PWR_WakeupClockRestore(void);

#endif /* __PWR_H__ */
