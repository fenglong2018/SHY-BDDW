#include "boot_confirm.h"
#include "system.h"
#include <string.h>

/* Boot Flag 区地址（与 Bootloader 保持一致） */
#define BOOT_FLAG_ADDR      0x0801F000UL
#define BOOT_FLAG_MAGIC     0xB007F1A6UL
#define UPGRADE_IDLE        0x00U
#define UPGRADE_PENDING     0xAAU
#define UPGRADE_OK          0x55U
#define UPGRADE_FAILED      0xFFU
#define FLASH_PAGE_SIZE     1024U

/* Flash 寄存器 */
typedef struct {
    volatile uint32_t ACR, KEYR, OPTKEYR, SR, CR, AR, RESERVED, OBR, WRPR;
} FLASH_T;
#define FLASH_REG   ((FLASH_T *)0x40022000UL)
#define FLASH_KEY1  0x45670123UL
#define FLASH_KEY2  0xCDEF89ABUL
#define FLASH_CR_PG    (1U << 0)
#define FLASH_CR_PER   (1U << 1)
#define FLASH_CR_STRT  (1U << 6)
#define FLASH_CR_LOCK  (1U << 7)
#define FLASH_SR_BSY   (1U << 0)

#pragma pack(1)
typedef struct {
    uint32_t magic;
    uint8_t  active_slot;
    uint8_t  upgrade_slot;
    uint8_t  upgrade_state;
    uint8_t  boot_fail_cnt;
    uint8_t  slot_a_ver[3];
    uint8_t  slot_b_ver[3];
    uint8_t  rsvd[2];
    uint32_t crc32;
} BootFlag_t;
#pragma pack()

static uint32_t _crc32(const uint8_t *buf, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    while (len--) {
        crc ^= *buf++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFFUL;
}

static void _flash_unlock(void)
{
    FLASH_REG->KEYR = FLASH_KEY1;
    FLASH_REG->KEYR = FLASH_KEY2;
}
static void _flash_lock(void) { FLASH_REG->CR |= FLASH_CR_LOCK; }
static void _flash_wait(void) { while (FLASH_REG->SR & FLASH_SR_BSY); }

static bool _flag_save(const BootFlag_t *flag)
{
    BootFlag_t tmp;
    memcpy(&tmp, flag, sizeof(tmp));
    tmp.crc32 = _crc32((const uint8_t *)&tmp, sizeof(tmp) - sizeof(uint32_t));

    /* 关中断，防止 Flash 写入被打断 */
    __asm volatile ("cpsid i");

    _flash_unlock();

    /* 擦除两页 */
    for (int p = 0; p < 2; p++) {
        _flash_wait();
        FLASH_REG->CR |= FLASH_CR_PER;
        FLASH_REG->AR  = BOOT_FLAG_ADDR + (uint32_t)(p * FLASH_PAGE_SIZE);
        FLASH_REG->CR |= FLASH_CR_STRT;
        _flash_wait();
        FLASH_REG->CR &= ~FLASH_CR_PER;
    }

    /* 写入 */
    const uint16_t *src  = (const uint16_t *)&tmp;
    uint32_t        addr = BOOT_FLAG_ADDR;
    for (size_t i = 0; i < sizeof(tmp) / 2; i++) {
        _flash_wait();
        FLASH_REG->CR |= FLASH_CR_PG;
        *(volatile uint16_t *)addr = src[i];
        _flash_wait();
        FLASH_REG->CR &= ~FLASH_CR_PG;
        addr += 2;
    }
    _flash_lock();

    __asm volatile ("cpsie i");
    return true;
}

void BootConfirm_OK(void)
{
    BootFlag_t flag;
    memcpy(&flag, (const void *)BOOT_FLAG_ADDR, sizeof(flag));

    if (flag.magic != BOOT_FLAG_MAGIC) return;

    /* 只在 PENDING 状态下确认 */
    if (flag.upgrade_state == UPGRADE_PENDING) {
        flag.upgrade_state = UPGRADE_OK;
        flag.boot_fail_cnt = 0;
        _flag_save(&flag);
    }
}

void BootConfirm_GetStatus(BootStatus_t *status)
{
    BootFlag_t flag;
    memcpy(&flag, (const void *)BOOT_FLAG_ADDR, sizeof(flag));

    if (flag.magic != BOOT_FLAG_MAGIC) {
        memset(status, 0, sizeof(BootStatus_t));
        return;
    }
    status->active_slot   = flag.active_slot;
    status->upgrade_state = flag.upgrade_state;
    memcpy(status->slot_a_ver, flag.slot_a_ver, 3);
    memcpy(status->slot_b_ver, flag.slot_b_ver, 3);
}
