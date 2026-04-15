#ifndef __USB_CDC_H__
#define __USB_CDC_H__

#include "system.h"

/**
 * USB CDC 虚拟串口
 * PA11 = USB_DM, PA12 = USB_DP
 * 插入 Type-C 后电脑识别为 COM 口，波特率无关（CDC 全速）
 */

void     USB_CDC_Init(void);
void     USB_CDC_SendByte(uint8_t byte);
void     USB_CDC_SendStr(const char *str);
void     USB_CDC_SendBuf(const uint8_t *buf, uint16_t len);
uint16_t USB_CDC_Available(void);
bool     USB_CDC_ReadByte(uint8_t *byte);
bool     USB_CDC_IsConnected(void);

/* IRQ，在启动文件弱符号基础上覆盖 */
void USB_LP_CAN1_RX0_IRQHandler(void);

#endif /* __USB_CDC_H__ */
