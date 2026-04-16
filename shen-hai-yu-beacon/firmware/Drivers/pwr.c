#include "pwr.h"

/* ---- 寄存器定义（全用标准 uint32_t） ---- */
typedef struct {
    volatile uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR;
    volatile uint32_t AHBENR, APB2ENR, APB1ENR, BDCR, CSR;
} RCC_T;
#define RCC  ((RCC_T *)0x40021000UL)

typedef struct { volatile uint32_t CR, CSR; } PWR_T;
#define PWR  ((PWR_T *)0x40007000UL)

typedef struct {
    volatile uint32_t CRH, CRL, PRLH, PRLL;
    volatile uint32_t DIVH, DIVL, CNTH, CNTL;
    volatile uint32_t ALRH, ALRL;
} RTC_T;
#define RTC  ((RTC_T *)0x40002800UL)

typedef struct {
    volatile uint32_t IMR, EMR, RTSR, FTSR, SWIER, PR;
} EXTI_T;
#define EXTI ((EXTI_T *)0x40010400UL)

#define SCB_SCR      (*(volatile uint32_t *)0xE000ED10UL)
#define NVIC_ISER1   (*(volatile uint32_t *)0xE000E104UL)

/* ---- RTC 辅助 ---- */
static void _rtc_wait_sync(void)
{
    RTC->CRL &= ~(1U << 3);
    while (!(RTC->CRL & (1U << 3)));
}

static void _rtc_wait_write(void)
{
    while (!(RTC->CRL & (1U << 5)));
}

static void _rtc_enter_cfg(void)
{
    _rtc_wait_write();
    RTC->CRL |= (1U << 4);
}

static void _rtc_exit_cfg(void)
{
    RTC->CRL &= ~(1U << 4);
    _rtc_wait_write();
}

/* ---- 初始化 ---- */
void PWR_Init(void)
{
    RCC->APB1ENR |= (1U << 28) | (1U << 27);  /* PWREN + BKPEN */

    /* 使能 LSI 40kHz */
    RCC->CSR |= (1U << 0);
    while (!(RCC->CSR & (1U << 1)));

    PWR->CR |= (1U << 8);  /* DBP=1 解除备份域写保护 */

    /* RTC 时钟源 = LSI */
    RCC->BDCR &= ~(3U << 8);
    RCC->BDCR |=  (2U << 8);
    RCC->BDCR |=  (1U << 15);  /* RTCEN */

    _rtc_wait_sync();

    /* 预分频 39999 → 1Hz */
    _rtc_enter_cfg();
    RTC->PRLH = 0x0000;
    RTC->PRLL = 39999;
    _rtc_exit_cfg();

    RTC->CRH |= (1U << 1);     /* ALRIE */

    EXTI->IMR  |= (1U << 17);  /* EXTI Line17 = RTC Alarm */
    EXTI->RTSR |= (1U << 17);

    NVIC_ISER1 |= (1U << (41 - 32));  /* RTCAlarm_IRQn = 41 */
}

/* ---- 定时休眠 ---- */
void PWR_EnterStopSeconds(uint32_t sleep_sec)
{
    uint32_t cnt   = ((uint32_t)RTC->CNTH << 16) | RTC->CNTL;
    uint32_t alarm = cnt + sleep_sec;

    _rtc_enter_cfg();
    RTC->ALRH = (uint16_t)(alarm >> 16);
    RTC->ALRL = (uint16_t)(alarm & 0xFFFFU);
    _rtc_exit_cfg();

    RTC->CRL &= ~(1U << 1);   /* 清 ALRF */
    EXTI->PR   =  (1U << 17);

    PWR->CR  = (PWR->CR & ~(1U << 1)) | (1U << 0);  /* LPDS=1, PDDS=0 */
    SCB_SCR |= (1U << 2);   /* SLEEPDEEP */

    __asm volatile ("wfi");

    SCB_SCR &= ~(1U << 2);
    PWR_WakeupClockRestore();
}

/* ---- 无限期休眠（等待 EXTI 按键唤醒） ---- */
void PWR_EnterStop(void)
{
    RTC->CRH &= ~(1U << 1);   /* 关闭 RTC 闹钟 */

    PWR->CR  = (PWR->CR & ~(1U << 1)) | (1U << 0);
    SCB_SCR |= (1U << 2);

    __asm volatile ("wfi");

    SCB_SCR &= ~(1U << 2);
    RTC->CRH |= (1U << 1);    /* 恢复闹钟中断 */
    PWR_WakeupClockRestore();
}

/* ---- 唤醒后恢复 PLL 时钟 ---- */
void PWR_WakeupClockRestore(void)
{
    RCC->CR |= (1U << 0);
    while (!(RCC->CR & (1U << 1)));

    RCC->CR |= (1U << 24);
    while (!(RCC->CR & (1U << 25)));

    RCC->CFGR = (RCC->CFGR & ~(3U << 0)) | (2U << 0);
    while ((RCC->CFGR & (3U << 2)) != (2U << 2));
}

/* ---- RTC 闹钟中断 ---- */
void RTCAlarm_IRQHandler(void)
{
    RTC->CRL &= ~(1U << 1);
    EXTI->PR   =  (1U << 17);
}
