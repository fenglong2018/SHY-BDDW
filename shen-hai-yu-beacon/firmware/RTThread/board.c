/**
 * @file  board.c
 * @brief RT-Thread BSP 板级支持 — N32L403KBQ7
 *        负责: 系统时钟、SysTick、rt_hw_console_output
 */

#include <rtthread.h>
#include "board.h"
#include "../Drivers/usb_cdc.h"
#include "../Drivers/uart.h"

/* ---- RCC 寄存器 ---- */
typedef struct {
    volatile rt_uint32_t CR, CFGR, CIR;
    volatile rt_uint32_t APB2RSTR, APB1RSTR;
    volatile rt_uint32_t AHBENR, APB2ENR, APB1ENR;
    volatile rt_uint32_t BDCR, CSR;
} RCC_T;
#define RCC  ((RCC_T *)0x40021000UL)

/* ---- SysTick ---- */
typedef struct {
    volatile rt_uint32_t CTRL, LOAD, VAL, CALIB;
} SysTick_T;
#define SYSTICK ((SysTick_T *)0xE000E010UL)

/* ---- NVIC ---- */
#define NVIC_ISER0  (*(volatile rt_uint32_t *)0xE000E100UL)
#define NVIC_IPR    ((volatile rt_uint8_t  *)0xE000E400UL)

/* ------------------------------------------------------------------ */
/*  系统时钟: HSI → PLL × 16 = 64 MHz                                 */
/* ------------------------------------------------------------------ */
static void SystemClock_Config(void)
{
    /* 使能 HSI */
    RCC->CR |= (1U << 0);
    while (!(RCC->CR & (1U << 1)));

    /* 切换到 HSI */
    RCC->CFGR &= ~(3U << 0);

    /* PLL: HSI/2 × 16 = 64 MHz */
    RCC->CFGR &= ~(0xFU << 18);
    RCC->CFGR |=  (0xEU << 18);   /* PLLMUL = ×16 */
    RCC->CFGR &= ~(1U  << 16);    /* PLLSRC = HSI/2 */

    /* APB1 = 32 MHz (÷2), APB2 = 64 MHz (÷1) */
    RCC->CFGR = (RCC->CFGR & ~(7U << 8)) | (4U << 8);
    RCC->CFGR &= ~(7U << 11);

    /* 使能 PLL */
    RCC->CR |= (1U << 24);
    while (!(RCC->CR & (1U << 25)));

    /* 切换到 PLL */
    RCC->CFGR = (RCC->CFGR & ~(3U << 0)) | (2U << 0);
    while ((RCC->CFGR & (3U << 2)) != (2U << 2));
}

/* ------------------------------------------------------------------ */
/*  RT-Thread 硬件初始化入口                                           */
/* ------------------------------------------------------------------ */
void rt_hw_board_init(void)
{
    SystemClock_Config();

    /* SysTick: 1 ms，使用 AHB 时钟 64 MHz */
    SYSTICK->LOAD = (64000000UL / RT_TICK_PER_SECOND) - 1U;
    SYSTICK->VAL  = 0U;
    SYSTICK->CTRL = (1U << 2) | (1U << 1) | (1U << 0);

    /* 初始化 USB CDC 控制台 */
    USB_CDC_Init();

#ifdef RT_USING_HEAP
    /* 将 BSS 结束到 RAM 末尾作为堆 */
    extern rt_uint8_t _ebss;
    rt_system_heap_init(&_ebss, (void *)(0x20000000UL + 32 * 1024));
#endif

    /* 板级组件初始化 */
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif
}

/* ------------------------------------------------------------------ */
/*  SysTick 中断 → RT-Thread tick                                      */
/* ------------------------------------------------------------------ */
void SysTick_Handler(void)
{
    rt_interrupt_enter();
    rt_tick_increase();
    UART_DMA_Poll();   /* 每 1ms 搬运 DMA 数据到软件环形缓冲 */
    rt_interrupt_leave();
}

/* ------------------------------------------------------------------ */
/*  控制台输出（rt_kprintf 底层）                                       */
/* ------------------------------------------------------------------ */
void rt_hw_console_output(const char *str)
{
    USB_CDC_SendStr(str);
}
