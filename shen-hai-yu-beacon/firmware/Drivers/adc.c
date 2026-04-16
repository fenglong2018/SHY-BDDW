#include "adc.h"

/* ---- 寄存器 ---- */
typedef struct {
    volatile uint32_t SR, CR1, CR2, SMPR1, SMPR2;
    volatile uint32_t JOFR1, JOFR2, JOFR3, JOFR4;
    volatile uint32_t HTR, LTR, SQR1, SQR2, SQR3;
    volatile uint32_t JSQR, JDR1, JDR2, JDR3, JDR4;
    volatile uint32_t DR;
} ADC_T;
#define ADC1 ((ADC_T *)0x40012400UL)

typedef struct {
    volatile uint32_t CR, CFGR, CIR;
    volatile uint32_t APB2RSTR, APB1RSTR;
    volatile uint32_t AHBENR, APB2ENR;
} RCC_T;
#define RCC ((RCC_T *)0x40021000UL)

typedef struct { volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR; } GPIO_T;
#define GPIOB_ADC ((GPIO_T *)0x40010C00UL)

/* ---- 450mAh 锂电池 OCV-SOC 查找表 ----
 * 列: [电压mV, 电量%]，从满到空
 */
static const uint16_t BAT_CURVE[][2] = {
    { 4200, 100 },
    { 4150,  97 },
    { 4100,  93 },
    { 4050,  88 },
    { 4000,  83 },
    { 3950,  77 },
    { 3900,  70 },
    { 3850,  63 },
    { 3800,  55 },
    { 3750,  47 },
    { 3700,  39 },
    { 3650,  31 },
    { 3600,  23 },
    { 3550,  16 },
    { 3500,  10 },
    { 3450,   5 },
    { 3400,   2 },
    { 3300,   0 },
};
#define BAT_CURVE_LEN  (sizeof(BAT_CURVE) / sizeof(BAT_CURVE[0]))

/* 分压比: (R23+R24)/R24 = (1.2M+3.3M)/3.3M ≈ 1.364 */
#define DIV_NUM  1364U
#define DIV_DEN  1000U

void ADC_Init(void)
{
    RCC->APB2ENR |= (1U << 9) | (1U << 3);  /* ADC1EN + IOPBEN */

    /* PB1 模拟输入 (CNF=00, MODE=00)，CRL bit[7:4] */
    GPIOB_ADC->CRL &= ~(0xFU << 4);

    ADC1->CR2   = (1U << 0);    /* ADON */
    ADC1->CR1   = 0;
    ADC1->SQR1  = 0;
    ADC1->SQR3  = 9;            /* CH9 = PB1 */
    ADC1->SMPR2 = (7U << 27);   /* CH9: SMPR2[29:27], 239.5 cycles */

    /* 校准 */
    ADC1->CR2 |= (1U << 3);
    while (ADC1->CR2 & (1U << 3));
    ADC1->CR2 |= (1U << 2);
    while (ADC1->CR2 & (1U << 2));
}

uint16_t ADC_Read(uint8_t channel)
{
    ADC1->SQR3 = channel & 0x1FU;
    ADC1->CR2 |= (1U << 22);                /* SWSTART */
    while (!(ADC1->SR & (1U << 1)));        /* 等待 EOC */
    return (uint16_t)(ADC1->DR & 0xFFFU);
}

uint16_t ADC_ReadBatMv(void)
{
    /* 16 次采样，去掉最大最小各2个，取中间12个均值（中位数滤波）*/
    uint16_t samples[16];
    for (int i = 0; i < 16; i++) samples[i] = ADC_Read(9);

    /* 简单冒泡排序（16个元素，开销极小）*/
    for (int i = 0; i < 15; i++)
        for (int j = 0; j < 15 - i; j++)
            if (samples[j] > samples[j+1]) {
                uint16_t t = samples[j];
                samples[j] = samples[j+1];
                samples[j+1] = t;
            }

    /* 去掉最小2个和最大2个，取中间12个均值 */
    uint32_t sum = 0;
    for (int i = 2; i < 14; i++) sum += samples[i];
    uint32_t adc_mv = (sum / 12U * 3300U) / 4095U;
    return (uint16_t)(adc_mv * DIV_NUM / DIV_DEN);
}

uint8_t ADC_BatPercent(void)
{
    uint16_t mv = ADC_ReadBatMv();
    return ADC_BatPercentEx(mv, false);
}

uint8_t ADC_BatPercentEx(uint16_t mv, bool is_charging)
{
    if (is_charging && mv > 80) mv -= 80;

    if (mv >= BAT_CURVE[0][0])                  return 100;
    if (mv <= BAT_CURVE[BAT_CURVE_LEN - 1][0]) return 0;

    for (uint8_t i = 0; i < BAT_CURVE_LEN - 1; i++) {
        uint16_t v_hi = BAT_CURVE[i][0];
        uint16_t v_lo = BAT_CURVE[i + 1][0];
        /* 精确匹配直接返回，避免除零 */
        if (mv == v_hi) return (uint8_t)BAT_CURVE[i][1];
        if (mv == v_lo) return (uint8_t)BAT_CURVE[i + 1][1];
        if (mv < v_hi && mv > v_lo) {
            uint8_t p_hi = (uint8_t)BAT_CURVE[i][1];
            uint8_t p_lo = (uint8_t)BAT_CURVE[i + 1][1];
            return (uint8_t)(p_lo + (uint32_t)(mv - v_lo) *
                             (p_hi - p_lo) / (v_hi - v_lo));
        }
    }
    return 0;
}
