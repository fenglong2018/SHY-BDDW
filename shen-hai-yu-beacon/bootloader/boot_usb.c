/**
 * @file  boot_usb.c
 * @brief Bootloader USB CDC 驱动（裁剪版，仅保留收发）
 * 直接复用 App 侧 usb_cdc.c 的实现，通过符号链接或复制
 * 这里提供一个轻量包装
 */
#include "boot_usb.h"

/* 直接包含 App 侧驱动源码（共享实现，避免重复维护） */
#include "../Drivers/usb_cdc.c"

void Boot_USB_Init(void)    { USB_CDC_Init(); }
void Boot_USB_SendByte(uint8_t b) { USB_CDC_SendByte(b); }
void Boot_USB_SendStr(const char *s) { USB_CDC_SendStr(s); }
bool Boot_USB_ReadByte(uint8_t *b)  { return USB_CDC_ReadByte(b); }
