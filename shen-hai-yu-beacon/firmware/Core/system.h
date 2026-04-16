#ifndef __SYSTEM_H__
#define __SYSTEM_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- 系统时钟 ---- */
#define SYS_CLOCK_HZ        64000000UL
#define APB1_CLOCK_HZ       32000000UL
#define APB2_CLOCK_HZ       64000000UL

/* ---- 基地址 ---- */
#define PERIPH_BASE         0x40000000UL
#define APB1_BASE           (PERIPH_BASE + 0x00000000UL)
#define APB2_BASE           (PERIPH_BASE + 0x00010000UL)
#define AHB_BASE            (PERIPH_BASE + 0x00020000UL)

/* ---- GPIO ---- */
#define GPIOA_BASE          (APB2_BASE + 0x0800UL)
#define GPIOB_BASE          (APB2_BASE + 0x0C00UL)
#define GPIOD_BASE          (APB2_BASE + 0x1400UL)

/* ---- USART ---- */
#define USART1_BASE         (APB2_BASE + 0x3800UL)
#define USART2_BASE         (APB1_BASE + 0x4400UL)
#define USART3_BASE         (APB1_BASE + 0x4800UL)

/* ---- ADC ---- */
#define ADC1_BASE           (APB2_BASE + 0x2400UL)

/* ---- RCC ---- */
#define RCC_BASE            (AHB_BASE  + 0x1000UL)

/* ---- SysTick ---- */
#define SYSTICK_BASE        0xE000E010UL

/* ---- 版本信息 ---- */
/* 软件版本：编译时固定，只读 */
#define FW_VERSION_MAJOR    1
#define FW_VERSION_MINOR    0
#define FW_VERSION_PATCH    0
#define FW_VERSION_STR      "V1.0.0"
/* 硬件版本：出厂时通过USB写入Flash，可读写 */
#define HW_VER_DEFAULT_STR  "H1.0.0"
#define PRODUCT_NAME        "ShenHaiYu-Beacon"

/**
 * 固件版本标记（放在 Flash 0x08004200，供 Bootloader 解析）
 * 用法：在任意 .c 文件中 #include "system.h" 后自动生成
 * Bootloader 在下载完成后读取此处版本号写入配置区
 */
#define FW_VERSION_MARKER() \
    __attribute__((used, section(".fw_version"))) \
    static const uint8_t _fw_ver_marker[] = { \
        0x53, 0x52, 0x45, 0x56,  /* "VERS" 魔数 小端 */ \
        FW_VERSION_MAJOR,         \
        FW_VERSION_MINOR,         \
        FW_VERSION_PATCH,         \
        0x00                      \
    }

/* ---- 系统函数 ---- */
void SystemInit(void);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);

#endif /* __SYSTEM_H__ */
