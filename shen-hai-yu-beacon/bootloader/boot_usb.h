#ifndef __BOOT_USB_H__
#define __BOOT_USB_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * Bootloader USB CDC 接口
 * 复用 App 侧的 usb_cdc 驱动，但独立编译
 */
void Boot_USB_Init(void);
void Boot_USB_SendByte(uint8_t byte);
void Boot_USB_SendStr(const char *str);
bool Boot_USB_ReadByte(uint8_t *byte);

#endif
