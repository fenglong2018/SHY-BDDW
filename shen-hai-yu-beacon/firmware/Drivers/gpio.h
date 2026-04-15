#ifndef __GPIO_H__
#define __GPIO_H__

#include "system.h"

/* ---- GPIO 寄存器 ---- */
typedef struct {
    volatile uint32_t CRL;   /* 配置低8位 */
    volatile uint32_t CRH;   /* 配置高8位 */
    volatile uint32_t IDR;   /* 输入数据 */
    volatile uint32_t ODR;   /* 输出数据 */
    volatile uint32_t BSRR;  /* 位设置/复位 */
    volatile uint32_t BRR;   /* 位复位 */
    volatile uint32_t LCKR;  /* 锁定 */
} GPIO_TypeDef;

#define GPIOA   ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB   ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOD   ((GPIO_TypeDef *)GPIOD_BASE)

/* ---- 引脚定义 ---- */
/* LED */
#define LED1_PORT       GPIOA
#define LED1_PIN        1       /* PA1 - 运行指示 */
#define LED2_PORT       GPIOB
#define LED2_PIN        4       /* PB4 - 电量指示 */
#define LED3_PORT       GPIOB
#define LED3_PIN        5       /* PB5 - 状态指示 */

/* 功耗控制 */
#define EN_PGNSS_PORT   GPIOB
#define EN_PGNSS_PIN    6       /* PB6 - GNSS 使能 */
#define EN_PRDSS_PORT   GPIOD
#define EN_PRDSS_PIN    14      /* PD14 - 短报文使能 */
#define EN_PA_PORT      GPIOA
#define EN_PA_PIN       6       /* PA6 - PA 功放使能 */
#define CVPOW5V_PORT    GPIOA
#define CVPOW5V_PIN     7       /* PA7 - 5V 功耗控制 */

/* 输入 */
#define MAGKEY_PORT     GPIOA
#define MAGKEY_PIN      5       /* PA5 - 磁控开关 */
#define KEY_FALL_PORT   GPIOB
#define KEY_FALL_PIN    7       /* PB7 - 落水检测 */
#define USB_IN_PORT     GPIOA
#define USB_IN_PIN      8       /* PA8 - USB 插入检测 */
#define BOOT_PORT       GPIOD
#define BOOT_PIN        0       /* PD0 - BOOT0 */

/* ---- API ---- */
void GPIO_Init(void);
void GPIO_SetPin(GPIO_TypeDef *port, uint8_t pin);
void GPIO_ResetPin(GPIO_TypeDef *port, uint8_t pin);
void GPIO_TogglePin(GPIO_TypeDef *port, uint8_t pin);
bool GPIO_ReadPin(GPIO_TypeDef *port, uint8_t pin);

/* LED 便捷宏 */
#define LED1_ON()       GPIO_ResetPin(LED1_PORT, LED1_PIN)
#define LED1_OFF()      GPIO_SetPin(LED1_PORT, LED1_PIN)
#define LED1_TOGGLE()   GPIO_TogglePin(LED1_PORT, LED1_PIN)
#define LED2_ON()       GPIO_ResetPin(LED2_PORT, LED2_PIN)
#define LED2_OFF()      GPIO_SetPin(LED2_PORT, LED2_PIN)
#define LED3_ON()       GPIO_ResetPin(LED3_PORT, LED3_PIN)
#define LED3_OFF()      GPIO_SetPin(LED3_PORT, LED3_PIN)

/* 模块使能宏 */
#define GNSS_POWER_ON()     GPIO_SetPin(EN_PGNSS_PORT, EN_PGNSS_PIN)
#define GNSS_POWER_OFF()    GPIO_ResetPin(EN_PGNSS_PORT, EN_PGNSS_PIN)
#define RDSS_POWER_ON()     GPIO_SetPin(EN_PRDSS_PORT, EN_PRDSS_PIN)
#define RDSS_POWER_OFF()    GPIO_ResetPin(EN_PRDSS_PORT, EN_PRDSS_PIN)
#define PA_POWER_ON()       GPIO_SetPin(EN_PA_PORT, EN_PA_PIN)
#define PA_POWER_OFF()      GPIO_ResetPin(EN_PA_PORT, EN_PA_PIN)

/* 输入读取宏 */
#define IS_MAGKEY_ACTIVE()  (!GPIO_ReadPin(MAGKEY_PORT, MAGKEY_PIN))
#define IS_FALL_DETECTED()  (!GPIO_ReadPin(KEY_FALL_PORT, KEY_FALL_PIN))
#define IS_USB_INSERTED()   (GPIO_ReadPin(USB_IN_PORT, USB_IN_PIN))

#endif /* __GPIO_H__ */
