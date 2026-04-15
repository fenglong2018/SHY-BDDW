#ifndef __UART_H__
#define __UART_H__

#include "system.h"

/* DMA 循环缓冲大小（RX 用 DMA，TX 用轮询）*/
#define UART_DMA_BUF_SIZE   256U   /* DMA 硬件循环缓冲 */
#define UART_RING_BUF_SIZE  512U   /* 软件环形缓冲（从DMA搬运） */

typedef struct {
    uint8_t  buf[UART_RING_BUF_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} RingBuf_t;

typedef enum {
    UART_CH1 = 0,   /* GNSS  - PA9/PA10,  DMA1 CH5 RX */
    UART_CH2 = 1,   /* RDSS  - PA2/PA0,   DMA1 CH6 RX */
    UART_CH3 = 2,   /* Debug - 无DMA，轮询 */
} UartCh_t;

void     UART_Init(UartCh_t ch, uint32_t baud);

/* TX：轮询发送 */
void     UART_SendByte(UartCh_t ch, uint8_t byte);
void     UART_SendStr(UartCh_t ch, const char *str);
void     UART_SendBuf(UartCh_t ch, const uint8_t *buf, uint16_t len);

/* RX：从软件环形缓冲读取（DMA 后台搬运） */
bool     UART_ReadByte(UartCh_t ch, uint8_t *byte);
uint16_t UART_Available(UartCh_t ch);
void     UART_Flush(UartCh_t ch);

/* DMA 搬运：在 SysTick 或定时器中调用，将 DMA 缓冲数据搬到环形缓冲 */
void     UART_DMA_Poll(void);

/* 调试：查询环形缓冲溢出次数（溢出说明线程处理太慢）*/
uint32_t UART_GetOverflow(UartCh_t ch);

void Debug_Printf(const char *fmt, ...);
#define DBG(fmt, ...)   Debug_Printf("[DBG] " fmt "\r\n", ##__VA_ARGS__)
#define LOG(fmt, ...)   Debug_Printf("[LOG] " fmt "\r\n", ##__VA_ARGS__)
#define ERR(fmt, ...)   Debug_Printf("[ERR] " fmt "\r\n", ##__VA_ARGS__)

#endif /* __UART_H__ */
