#include "boot_flash.h"
#include <string.h>

typedef struct {
    volatile uint32_t ACR, KEYR, OPTKEYR, SR, CR, AR, RESERVED, OBR, WRPR;
} FLASH_T;
#define FLASH_REG   ((FLASH_T *)0x40022000UL)

#define FLASH_KEY1      0x45670123UL
#define FLASH_KEY2      0xCDEF89ABUL
#define FLASH_SR_BSY    (1U << 0)
#define FLASH_SR_EOP    (1U << 5)
#define FLASH_SR_PGERR  (1U << 2)
#define FLASH_SR_WRPERR (1U << 4)
#define FLASH_CR_PG     (1U << 0)
#define FLASH_CR_PER    (1U << 1)
#define FLASH_CR_STRT   (1U << 6)
#define FLASH_CR_LOCK   (1U << 7)

static void _unlock(void)
{
    FLASH_REG->KEYR = FLASH_KEY1;
    FLASH_REG->KEYR = FLASH_KEY2;
}
static void _lock(void) { FLASH_REG->CR |= FLASH_CR_LOCK; }
static void _wait(void) { while (FLASH_REG->SR & FLASH_SR_BSY); }

bool BootFlash_ErasePage(uint32_t addr)
{
    _unlock();
    _wait();
    FLASH_REG->SR  = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPERR;
    FLASH_REG->CR |= FLASH_CR_PER;
    FLASH_REG->AR  = addr;
    FLASH_REG->CR |= FLASH_CR_STRT;
    _wait();
    FLASH_REG->CR &= ~FLASH_CR_PER;
    bool ok = !(FLASH_REG->SR & (FLASH_SR_PGERR | FLASH_SR_WRPERR));
    _lock();
    return ok;
}

bool BootFlash_WriteHWord(uint32_t addr, uint16_t data)
{
    _unlock();
    _wait();
    FLASH_REG->CR |= FLASH_CR_PG;
    *(volatile uint16_t *)addr = data;
    _wait();
    FLASH_REG->CR &= ~FLASH_CR_PG;
    bool ok = (*(volatile uint16_t *)addr == data);
    _lock();
    return ok;
}

bool BootFlash_WriteBlock(uint32_t addr, const uint8_t *data, uint32_t len)
{
    /* 按页擦除 */
    uint32_t page_start = addr & ~(FLASH_PAGE_SIZE - 1);
    uint32_t page_end   = (addr + len - 1) & ~(FLASH_PAGE_SIZE - 1);
    for (uint32_t p = page_start; p <= page_end; p += FLASH_PAGE_SIZE) {
        if (!BootFlash_ErasePage(p)) return false;
    }

    /* 按半字写入 */
    _unlock();
    uint32_t dst = addr;
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t hw = (uint16_t)data[i];
        if (i + 1 < len) hw |= (uint16_t)(data[i + 1] << 8);
        _wait();
        FLASH_REG->CR |= FLASH_CR_PG;
        *(volatile uint16_t *)dst = hw;
        _wait();
        FLASH_REG->CR &= ~FLASH_CR_PG;
        if (*(volatile uint16_t *)dst != hw) { _lock(); return false; }
        dst += 2;
    }
    _lock();
    return true;
}

/* ---- CRC32 ---- */
uint32_t BootFlash_CRC32(const uint8_t *buf, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    while (len--) {
        crc ^= *buf++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* ---- 更新配置区软件版本 ---- */
bool BootFlash_UpdateFwVersion(uint8_t major, uint8_t minor, uint8_t patch)
{
    BootFlashCfg_t cfg;
    memcpy(&cfg, (const void *)FLASH_CFG_ADDR, sizeof(cfg));

    /* 如果配置区无效，初始化默认值 */
    if (cfg.magic != FLASH_CFG_MAGIC) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = FLASH_CFG_MAGIC;
        memcpy(cfg.sn,  "SHY-0000-000000", 15);
        memcpy(cfg.uid, "0000000000000000", 16);
        cfg.hw_ver[0] = 1;
    }

    /* 写入新软件版本到 hw_ver[3] 位置后的扩展字段
     * 注意：hw_ver 存硬件版本，软件版本单独用 rsvd[0~2] 暂存
     * 实际上 rsvd 字段用于存 fw_ver */
    cfg.rsvd[0] = major;
    cfg.rsvd[1] = minor;
    cfg.rsvd[2] = patch;

    /* 重新计算 CRC（不含 crc32 字段本身） */
    cfg.crc32 = BootFlash_CRC32((const uint8_t *)&cfg,
                                sizeof(cfg) - sizeof(uint32_t));

    /* 擦除并写入配置页 */
    if (!BootFlash_ErasePage(FLASH_CFG_ADDR)) return false;

    const uint16_t *src = (const uint16_t *)&cfg;
    uint32_t addr = FLASH_CFG_ADDR;
    _unlock();
    for (size_t i = 0; i < sizeof(cfg) / 2; i++) {
        _wait();
        FLASH_REG->CR |= FLASH_CR_PG;
        *(volatile uint16_t *)addr = src[i];
        _wait();
        FLASH_REG->CR &= ~FLASH_CR_PG;
        addr += 2;
    }
    _lock();
    return true;
}
