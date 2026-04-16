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
#define LED1_PORT       GPIOD
#define LED1_PIN        15      /* PD15 - 运行指示 */
#define LED2_PORT       GPIOB
#define LED2_PIN        5       /* PB5 - 电量指示 */
#define LED3_PORT       GPIOB
#define LED3_PIN        3       /* PB3 - 状态指示 */

/* 功耗控制 */
#define EN_PGNSS_PORT   GPIOB
#define EN_PGNSS_PIN    6       /* PB6 - GNSS 使能，高电平开 */
#define EN_PRDSS_PORT   GPIOD
#define EN_PRDSS_PIN    14      /* PD14 - 短报文使能，高电平开 */
#define CVPOW5V_PORT    GPIOA
#define CVPOW5V_PIN     2       /* PA2 - RDSS PA 电源，高电平开 */

/* 输入 */
#define MAGKEY_PORT     GPIOA
#define MAGKEY_PIN      7       /* PA7 - 磁控开关 */
#define KEY_FALL_PORT   GPIOB
#define KEY_FALL_PIN    4       /* PB4 - 落水检测 / 按键 */
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

/* LED 便捷宏（低电平点亮） */
#define LED1_ON()       GPIO_ResetPin(LED1_PORT, LED1_PIN)
#define LED1_OFF()      GPIO_SetPin(LED1_PORT, LED1_PIN)
#define LED1_TOGGLE()   GPIO_TogglePin(LED1_PORT, LED1_PIN)
#define LED2_ON()       GPIO_ResetPin(LED2_PORT, LED2_PIN)
#define LED2_OFF()      GPIO_SetPin(LED2_PORT, LED2_PIN)
#define LED3_ON()       GPIO_ResetPin(LED3_PORT, LED3_PIN)
#define LED3_OFF()      GPIO_SetPin(LED3_PORT, LED3_PIN)

/* 模块使能宏
 * CVPOW5V  (PA2): 高电平开，低电平关  → RDSS PA 电源
 * EN_PGNSS (PB6): 高电平开，低电平关  → GNSS 模块电源
 * EN_PRDSS (PD14):高电平开，低电平关  → 短报文模块电源
 *
 * 开启顺序：先拉高 CVPOW5V（PA 上电），再拉高 EN_PGNSS/EN_PRDSS
 * 关闭顺序：先拉低 EN_PGNSS/EN_PRDSS，再拉低 CVPOW5V
 */
#define GNSS_POWER_ON()  do { \
    GPIO_SetPin(CVPOW5V_PORT, CVPOW5V_PIN);     /* CVPOW5V 高电平，5V上电 */ \
    GPIO_SetPin(EN_PGNSS_PORT, EN_PGNSS_PIN);   /* EN_PGNSS 高电平，使能 */ \
} while(0)

#define GNSS_POWER_OFF() do { \
    GPIO_ResetPin(EN_PGNSS_PORT, EN_PGNSS_PIN); /* EN_PGNSS 低电平，关闭 */ \
} while(0)

#define RDSS_POWER_ON()  do { \
    GPIO_SetPin(CVPOW5V_PORT, CVPOW5V_PIN);     /* CVPOW5V 高电平，5V上电 */ \
    GPIO_SetPin(EN_PRDSS_PORT, EN_PRDSS_PIN);   /* EN_PRDSS 高电平，使能 */ \
} while(0)

#define RDSS_POWER_OFF() do { \
    GPIO_ResetPin(EN_PRDSS_PORT, EN_PRDSS_PIN); /* EN_PRDSS 低电平，关闭 */ \
} while(0)

/* 两个模块都关闭后才关 CVPOW5V */
#define ALL_MODULE_POWER_OFF() do { \
    GPIO_ResetPin(EN_PGNSS_PORT, EN_PGNSS_PIN); /* EN_PGNSS 低电平，关闭 */ \
    GPIO_ResetPin(EN_PRDSS_PORT, EN_PRDSS_PIN); /* EN_PRDSS 低电平，关闭 */ \
    GPIO_ResetPin(CVPOW5V_PORT,  CVPOW5V_PIN);  /* CVPOW5V  低电平，5V断电 */ \
} while(0)

/* 输入读取宏
 * USB_IN  (PA8): 插入=低电平，未插=高电平
 * KEY_FALL(PB4): 落水/按下=高电平，正常/松开=低电平
 * MAGKEY  (PA7): 触发=低电平（霍尔传感器，磁铁靠近拉低）
 */
#define IS_MAGKEY_ACTIVE()  (!GPIO_ReadPin(MAGKEY_PORT, MAGKEY_PIN))  /* 低电平触发 */
#define IS_FALL_DETECTED()  (GPIO_ReadPin(KEY_FALL_PORT, KEY_FALL_PIN)) /* 高电平=落水/按下 */
#define IS_USB_INSERTED()   (!GPIO_ReadPin(USB_IN_PORT, USB_IN_PIN))    /* 低电平=已插入 */

#endif /* __GPIO_H__ */
