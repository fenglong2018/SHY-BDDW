#include "gpio.h"

/* RCC APB2ENR 位定义 */
#define RCC_APB2ENR_IOPAEN  (1U << 2)
#define RCC_APB2ENR_IOPBEN  (1U << 3)
#define RCC_APB2ENR_IOPDEN  (1U << 5)

typedef struct {
    volatile uint32_t CR;
    volatile uint32_t CFGR;
    volatile uint32_t CIR;
    volatile uint32_t APB2RSTR;
    volatile uint32_t APB1RSTR;
    volatile uint32_t AHBENR;
    volatile uint32_t APB2ENR;
    volatile uint32_t APB1ENR;
} RCC_TypeDef;
#define RCC ((RCC_TypeDef *)RCC_BASE)

/* 配置引脚为推挽输出 2MHz */
static void _gpio_output(GPIO_TypeDef *port, uint8_t pin)
{
    volatile uint32_t *cr = (pin < 8) ? &port->CRL : &port->CRH;
    uint8_t shift = (pin < 8) ? (pin * 4) : ((pin - 8) * 4);
    *cr &= ~(0xFU << shift);
    *cr |=  (0x2U << shift); /* 输出 2MHz 推挽 */
}

/* 配置引脚为上拉输入 */
static void _gpio_input_pullup(GPIO_TypeDef *port, uint8_t pin)
{
    volatile uint32_t *cr = (pin < 8) ? &port->CRL : &port->CRH;
    uint8_t shift = (pin < 8) ? (pin * 4) : ((pin - 8) * 4);
    *cr &= ~(0xFU << shift);
    *cr |=  (0x8U << shift); /* 输入，上拉/下拉 */
    port->ODR |= (1U << pin); /* 上拉 */
}

/* 配置引脚为下拉输入 */
static void _gpio_input_pulldown(GPIO_TypeDef *port, uint8_t pin)
{
    volatile uint32_t *cr = (pin < 8) ? &port->CRL : &port->CRH;
    uint8_t shift = (pin < 8) ? (pin * 4) : ((pin - 8) * 4);
    *cr &= ~(0xFU << shift);
    *cr |=  (0x8U << shift); /* 输入，上拉/下拉模式 */
    port->BRR = (1U << pin); /* ODR=0 → 下拉 */
}

void GPIO_Init(void)
{
    /* 使能 GPIOA/B/D 时钟 */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPDEN;

    /* LED 输出，默认关闭（高电平） */
    _gpio_output(LED1_PORT, LED1_PIN);   LED1_OFF();  /* PD15 */
    _gpio_output(LED2_PORT, LED2_PIN);   LED2_OFF();  /* PB5  */
    _gpio_output(LED3_PORT, LED3_PIN);   LED3_OFF();  /* PB3  */

    /* 模块使能输出，默认全部关闭
     * EN_PGNSS (PB6) : 高电平开，初始化为低电平（关闭）
     * EN_PRDSS (PD14): 高电平开，初始化为低电平（关闭）
     * CVPOW5V  (PA2) : 高电平开，初始化为低电平（关闭）
     */
    _gpio_output(EN_PGNSS_PORT, EN_PGNSS_PIN);
    GPIO_ResetPin(EN_PGNSS_PORT, EN_PGNSS_PIN);  /* 低电平 = 关闭 */

    _gpio_output(EN_PRDSS_PORT, EN_PRDSS_PIN);
    GPIO_ResetPin(EN_PRDSS_PORT, EN_PRDSS_PIN);  /* 低电平 = 关闭 */

    _gpio_output(CVPOW5V_PORT, CVPOW5V_PIN);
    GPIO_ResetPin(CVPOW5V_PORT, CVPOW5V_PIN);    /* 低电平 = 关闭 */

    /* 输入引脚 */
    /* MAGKEY (PA7): 磁控开关，低电平触发，上拉 */
    _gpio_input_pullup(MAGKEY_PORT, MAGKEY_PIN);

    /* KEY_FALL (PB4): 落水/SOS，高电平触发，下拉
     * 正常状态低电平，落水或按下时变高电平 */
    _gpio_input_pulldown(KEY_FALL_PORT, KEY_FALL_PIN);

    /* USB_IN (PA8): USB插入检测，低电平=已插入，上拉
     * 未插时上拉为高，插入后被拉低 */
    _gpio_input_pullup(USB_IN_PORT, USB_IN_PIN);
}

void GPIO_SetPin(GPIO_TypeDef *port, uint8_t pin)
{
    port->BSRR = (1U << pin);
}

void GPIO_ResetPin(GPIO_TypeDef *port, uint8_t pin)
{
    port->BRR = (1U << pin);
}

void GPIO_TogglePin(GPIO_TypeDef *port, uint8_t pin)
{
    port->ODR ^= (1U << pin);
}

bool GPIO_ReadPin(GPIO_TypeDef *port, uint8_t pin)
{
    return (port->IDR & (1U << pin)) != 0;
}
