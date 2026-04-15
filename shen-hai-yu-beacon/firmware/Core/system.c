#include "system.h"

static volatile uint32_t s_tick = 0;

/* RCC 寄存器简化访问 */
typedef struct {
    volatile uint32_t CR;
    volatile uint32_t CFGR;
    volatile uint32_t CIR;
    volatile uint32_t APB2RSTR;
    volatile uint32_t APB1RSTR;
    volatile uint32_t AHBENR;
    volatile uint32_t APB2ENR;
    volatile uint32_t APB1ENR;
    volatile uint32_t BDCR;
    volatile uint32_t CSR;
} RCC_TypeDef;

#define RCC     ((RCC_TypeDef *)RCC_BASE)

/* SysTick 寄存器 */
typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
} SysTick_TypeDef;

#define SYSTICK ((SysTick_TypeDef *)SYSTICK_BASE)

void SystemInit(void)
{
    /* 使能 HSI，等待就绪 */
    RCC->CR |= (1U << 0);
    while (!(RCC->CR & (1U << 1)));

    /* 切换到 HSI */
    RCC->CFGR &= ~(3U << 0);

    /* 配置 PLL: HSI/2 * 16 = 64MHz */
    RCC->CFGR &= ~(0xFU << 18);
    RCC->CFGR |= (0xEU << 18);  /* PLLMUL = x16 */
    RCC->CFGR &= ~(1U << 16);   /* PLLSRC = HSI/2 */

    /* 使能 PLL */
    RCC->CR |= (1U << 24);
    while (!(RCC->CR & (1U << 25)));

    /* APB1 = SYSCLK/2, APB2 = SYSCLK/1 */
    RCC->CFGR |= (4U << 8);
    RCC->CFGR &= ~(7U << 11);

    /* 切换到 PLL */
    RCC->CFGR |= (2U << 0);
    while ((RCC->CFGR & (3U << 2)) != (2U << 2));

    /* 配置 SysTick: 1ms 中断 */
    SYSTICK->LOAD = (SYS_CLOCK_HZ / 1000U) - 1U;
    SYSTICK->VAL  = 0U;
    SYSTICK->CTRL = (1U << 2) | (1U << 1) | (1U << 0); /* CLKSRC=AHB, TICKINT, ENABLE */
}

uint32_t HAL_GetTick(void)
{
    return s_tick;
}

void HAL_Delay(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < ms);
}
