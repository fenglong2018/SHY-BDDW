#include "boot_hw.h"
#include <stdarg.h>
#include <stdio.h>

/* ---- 寄存器 ---- */
typedef struct {
    volatile uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR;
    volatile uint32_t AHBENR, APB2ENR, APB1ENR, BDCR, CSR;
} RCC_T;
#define RCC ((RCC_T *)0x40021000UL)

typedef struct {
    volatile uint32_t CTRL, LOAD, VAL, CALIB;
} SYSTICK_T;
#define SYSTICK ((SYSTICK_T *)0xE000E010UL)

typedef struct {
    volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} GPIO_T;
#define GPIOB ((GPIO_T *)0x40010C00UL)

static volatile uint32_t s_tick = 0;

/* 启动文件需要 SystemInit，Bootloader 在 Boot_HW_Init 里完成时钟配置 */
void SystemInit(void) {}

void Boot_HW_Init(void)
{
    /* HSI → PLL × 16 = 64MHz */
    RCC->CR |= (1U << 0);
    while (!(RCC->CR & (1U << 1)));
    RCC->CFGR &= ~(3U << 0);
    RCC->CFGR = (RCC->CFGR & ~(0xFU << 18)) | (0xEU << 18);
    RCC->CFGR &= ~(1U << 16);
    RCC->CFGR = (RCC->CFGR & ~(7U << 8)) | (4U << 8);
    RCC->CR |= (1U << 24);
    while (!(RCC->CR & (1U << 25)));
    RCC->CFGR = (RCC->CFGR & ~(3U << 0)) | (2U << 0);
    while ((RCC->CFGR & (3U << 2)) != (2U << 2));

    /* SysTick 1ms */
    SYSTICK->LOAD = 64000UL - 1;
    SYSTICK->VAL  = 0;
    SYSTICK->CTRL = (1U << 2) | (1U << 1) | (1U << 0);

    /* PB7 上拉输入（SOS键） */
    RCC->APB2ENR |= (1U << 3);
    GPIOB->CRL &= ~(0xFU << 28);
    GPIOB->CRL |=  (0x8U << 28);
    GPIOB->ODR |=  (1U << 7);
}

void SysTick_Handler(void) { s_tick++; }

uint32_t Boot_GetTick(void) { return s_tick; }

void Boot_DelayMs(uint32_t ms)
{
    uint32_t t = s_tick;
    while ((s_tick - t) < ms);
}

bool Boot_IsKeyPressed(void)
{
    return !(GPIOB->IDR & (1U << 7));
}

void Boot_Snprintf(char *buf, size_t len, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, len, fmt, args);
    va_end(args);
}
