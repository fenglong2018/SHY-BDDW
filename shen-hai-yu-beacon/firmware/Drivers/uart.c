/**
 * @file  uart.c
 * @brief UART 驱动
 *
 * RX 策略：
 *   UART1/UART2 使用 DMA1 循环模式接收，CPU 零参与
 *   UART3 使用 RXNE 中断接收（调试口，数据量小）
 *
 * TX 策略：
 *   全部轮询发送（发送频率极低，无需 DMA）
 *
 * DMA 通道分配（N32L403）：
 *   USART1_RX → DMA1 Channel 5
 *   USART2_RX → DMA1 Channel 6
 *
 * UART_DMA_Poll() 需在 RT-Thread SysTick 钩子或专用线程中定期调用，
 * 将 DMA 硬件缓冲中的新数据搬运到软件环形缓冲。
 */

#include "uart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- 寄存器定义 ---- */
typedef struct {
    volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
} USART_T;

typedef struct {
    volatile uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR;
    volatile uint32_t AHBENR, APB2ENR, APB1ENR;
} RCC_T;

/* DMA 通道寄存器 */
typedef struct {
    volatile uint32_t CCR;    /* 配置 */
    volatile uint32_t CNDTR;  /* 数据数量 */
    volatile uint32_t CPAR;   /* 外设地址 */
    volatile uint32_t CMAR;   /* 内存地址 */
    volatile uint32_t RESERVED;
} DMA_CH_T;

typedef struct {
    volatile uint32_t ISR;   /* 中断状态 */
    volatile uint32_t IFCR;  /* 中断标志清除 */
} DMA_T;

#define USART1  ((USART_T *)USART1_BASE)
#define USART2  ((USART_T *)USART2_BASE)
#define USART3  ((USART_T *)USART3_BASE)
#define RCC     ((RCC_T *)RCC_BASE)

#define DMA1        ((DMA_T *)0x40020000UL)
#define DMA1_CH5    ((DMA_CH_T *)0x40020058UL)  /* USART1_RX */
#define DMA1_CH6    ((DMA_CH_T *)0x40020068UL)  /* USART2_RX */

typedef struct {
    volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} GPIO_T;
#define GPIOA_U ((GPIO_T *)GPIOA_BASE)
#define GPIOB_U ((GPIO_T *)GPIOB_BASE)

/* ---- DMA 硬件循环缓冲（静态分配）---- */
static uint8_t s_dma_buf1[UART_DMA_BUF_SIZE];  /* USART1 RX DMA */
static uint8_t s_dma_buf2[UART_DMA_BUF_SIZE];  /* USART2 RX DMA */

/* DMA 上次读取位置（用于计算新增数据） */
static uint16_t s_dma_last1 = 0;
static uint16_t s_dma_last2 = 0;

/* 软件环形缓冲 */
static RingBuf_t s_rxbuf[3];

static USART_T *s_usart[3] = { USART1, USART2, USART3 };

/* ---- 环形缓冲操作 ---- */
static uint32_t s_rb_overflow[3] = {0};  /* 各通道环形缓冲溢出计数 */

static void _rb_push(RingBuf_t *rb, uint8_t byte)
{
    if (rb->count < UART_RING_BUF_SIZE) {
        rb->buf[rb->tail] = byte;
        rb->tail = (rb->tail + 1) % UART_RING_BUF_SIZE;
        rb->count++;
    } else {
        /* 统计溢出，供调试使用 */
        s_rb_overflow[rb == &s_rxbuf[0] ? 0 : (rb == &s_rxbuf[1] ? 1 : 2)]++;
    }
}

static bool _rb_pop(RingBuf_t *rb, uint8_t *byte)
{
    if (rb->count == 0) return false;
    *byte = rb->buf[rb->head];
    rb->head = (rb->head + 1) % UART_RING_BUF_SIZE;
    rb->count--;
    return true;
}

/* ---- DMA 初始化（循环模式，外设→内存）---- */
static void _dma_rx_init(DMA_CH_T *ch, uint32_t usart_dr_addr,
                          uint8_t *mem_buf, uint16_t buf_size)
{
    /* 使能 DMA1 时钟 */
    RCC->AHBENR |= (1U << 0);

    ch->CCR   = 0;           /* 先关闭 */
    ch->CNDTR = buf_size;
    ch->CPAR  = usart_dr_addr;
    ch->CMAR  = (uint32_t)mem_buf;

    /* CCR: 循环模式, 外设→内存, 内存地址自增, 8bit, 不使能中断 */
    ch->CCR = (1U << 5)  /* CIRC: 循环模式 */
            | (0U << 4)  /* DIR: 外设→内存 */
            | (1U << 7)  /* MINC: 内存地址自增 */
            | (0U << 6)  /* PINC: 外设地址不增 */
            | (0U << 8)  /* MSIZE: 8bit */
            | (0U << 10) /* PSIZE: 8bit */
            | (1U << 0); /* EN: 使能 */
}

/* ---- UART 初始化 ---- */
void UART_Init(UartCh_t ch, uint32_t baud)
{
    USART_T *u = s_usart[ch];
    uint32_t clk;

    memset(&s_rxbuf[ch], 0, sizeof(RingBuf_t));

    if (ch == UART_CH1) {
        RCC->APB2ENR |= (1U << 14) | (1U << 2);
        GPIOA_U->CRH &= ~(0xFFU << 4);
        GPIOA_U->CRH |=  (0xBU  << 4);   /* PA9  AF PP 50MHz */
        GPIOA_U->CRH &= ~(0xFU  << 8);
        GPIOA_U->CRH |=  (0x4U  << 8);   /* PA10 浮空输入 */
        clk = APB2_CLOCK_HZ;

        /* 配置 DMA1 CH5 接收 USART1_RX */
        _dma_rx_init(DMA1_CH5, (uint32_t)&USART1->DR,
                     s_dma_buf1, UART_DMA_BUF_SIZE);
        s_dma_last1 = 0;

        /* 使能 USART1 DMA RX 请求 */
        u->CR3 |= (1U << 6);  /* DMAR */

    } else if (ch == UART_CH2) {
        RCC->APB1ENR |= (1U << 17);
        RCC->APB2ENR |= (1U << 2);
        GPIOA_U->CRL &= ~(0xFFU << 8);
        GPIOA_U->CRL |=  (0xBU  << 8);   /* PA2 AF PP 50MHz */
        GPIOA_U->CRL &= ~(0xFU  << 0);
        GPIOA_U->CRL |=  (0x4U  << 0);   /* PA0 浮空输入 */
        clk = APB1_CLOCK_HZ;

        /* 配置 DMA1 CH6 接收 USART2_RX */
        _dma_rx_init(DMA1_CH6, (uint32_t)&USART2->DR,
                     s_dma_buf2, UART_DMA_BUF_SIZE);
        s_dma_last2 = 0;

        /* 使能 USART2 DMA RX 请求 */
        u->CR3 |= (1U << 6);  /* DMAR */

    } else {
        /* UART3：调试口，使用 RXNE 中断 */
        RCC->APB1ENR |= (1U << 18);
        RCC->APB2ENR |= (1U << 3);
        clk = APB1_CLOCK_HZ;
    }

    u->BRR = clk / baud;

    if (ch == UART_CH3) {
        /* UART3 使能 RXNE 中断 */
        u->CR1 = (1U << 13) | (1U << 3) | (1U << 2) | (1U << 5);
        /* NVIC 使能 USART3 (IRQ 39) */
        (*(volatile uint32_t *)0xE000E104UL) |= (1U << (39 - 32));
    } else {
        /* UART1/2 只使能 TX/RX，不使能 RXNE 中断（DMA 接管） */
        u->CR1 = (1U << 13) | (1U << 3) | (1U << 2);
    }
}

/* ---- DMA 搬运：将 DMA 循环缓冲中的新数据搬到软件环形缓冲 ----
 * 在 SysTick_Handler 中调用（中断上下文），环形缓冲写操作本身是原子的，
 * 线程只读 head/count，SysTick 只写 tail/count，方向不同，无需额外锁。
 * 但 count 字段需要用 volatile 保证可见性（已在结构体中声明）。
 */
void UART_DMA_Poll(void)
{
    /* USART1：DMA1 CH5 */
    {
        uint16_t cur  = (uint16_t)(UART_DMA_BUF_SIZE - DMA1_CH5->CNDTR);
        uint16_t last = s_dma_last1;
        if (cur != last) {
            if (cur > last) {
                for (uint16_t i = last; i < cur; i++)
                    _rb_push(&s_rxbuf[UART_CH1], s_dma_buf1[i]);
            } else {
                /* 循环回绕 */
                for (uint16_t i = last; i < UART_DMA_BUF_SIZE; i++)
                    _rb_push(&s_rxbuf[UART_CH1], s_dma_buf1[i]);
                for (uint16_t i = 0; i < cur; i++)
                    _rb_push(&s_rxbuf[UART_CH1], s_dma_buf1[i]);
            }
            s_dma_last1 = cur;
        }
    }

    /* USART2：DMA1 CH6 */
    {
        uint16_t cur  = (uint16_t)(UART_DMA_BUF_SIZE - DMA1_CH6->CNDTR);
        uint16_t last = s_dma_last2;
        if (cur != last) {
            if (cur > last) {
                for (uint16_t i = last; i < cur; i++)
                    _rb_push(&s_rxbuf[UART_CH2], s_dma_buf2[i]);
            } else {
                for (uint16_t i = last; i < UART_DMA_BUF_SIZE; i++)
                    _rb_push(&s_rxbuf[UART_CH2], s_dma_buf2[i]);
                for (uint16_t i = 0; i < cur; i++)
                    _rb_push(&s_rxbuf[UART_CH2], s_dma_buf2[i]);
            }
            s_dma_last2 = cur;
        }
    }
}

/* ---- TX：轮询发送 ---- */
void UART_SendByte(UartCh_t ch, uint8_t byte)
{
    USART_T *u = s_usart[ch];
    while (!(u->SR & (1U << 7)));
    u->DR = byte;
}

void UART_SendStr(UartCh_t ch, const char *str)
{
    while (*str) UART_SendByte(ch, (uint8_t)*str++);
}

void UART_SendBuf(UartCh_t ch, const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) UART_SendByte(ch, buf[i]);
}

/* ---- RX：从软件环形缓冲读取 ---- */
bool UART_ReadByte(UartCh_t ch, uint8_t *byte)
{
    return _rb_pop(&s_rxbuf[ch], byte);
}

uint16_t UART_Available(UartCh_t ch)
{
    return s_rxbuf[ch].count;
}

void UART_Flush(UartCh_t ch)
{
    memset(&s_rxbuf[ch], 0, sizeof(RingBuf_t));
    if (ch == UART_CH1) s_dma_last1 = (uint16_t)(UART_DMA_BUF_SIZE - DMA1_CH5->CNDTR);
    if (ch == UART_CH2) s_dma_last2 = (uint16_t)(UART_DMA_BUF_SIZE - DMA1_CH6->CNDTR);
}

/* ---- UART3 RXNE 中断（调试口）---- */
void USART3_IRQHandler(void)
{
    if (USART3->SR & (1U << 5))
        _rb_push(&s_rxbuf[UART_CH3], (uint8_t)USART3->DR);
}

uint32_t UART_GetOverflow(UartCh_t ch)
{
    return (ch < 3) ? s_rb_overflow[ch] : 0;
}

/* ---- Debug Printf ---- */
void Debug_Printf(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    UART_SendStr(UART_CH3, buf);
}
