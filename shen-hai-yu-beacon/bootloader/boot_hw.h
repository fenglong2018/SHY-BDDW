#ifndef __BOOT_HW_H__
#define __BOOT_HW_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BOOT_FW_VER  "BL1.0.0"

void     Boot_HW_Init(void);
uint32_t Boot_GetTick(void);
void     Boot_DelayMs(uint32_t ms);
bool     Boot_IsKeyPressed(void);   /* PB7 低有效 */
void     Boot_Snprintf(char *buf, size_t len, const char *fmt, ...);

#endif
