#include "flash_cfg.h"
#include <string.h>

FlashCfg_t g_cfg;

/* ---- Flash 寄存器 ---- */
typedef struct {
    volatile uint32_t ACR;
    volatile uint32_t KEYR;
    volatile uint32_t OPTKEYR;
    volatile uint32_t SR;
    volatile uint32_t CR;
    volatile uint32_t AR;
    volatile uint32_t RESERVED;
    volatile uint32_t OBR;
    volatile uint32_t WRPR;
} FLASH_T;
#define FLASH_REG   ((FLASH_T *)0x40022000UL)

#define FLASH_KEY1  0x45670123UL
#define FLASH_KEY2  0xCDEF89ABUL
#define FLASH_SR_BSY    (1U << 0)
#define FLASH_SR_EOP    (1U << 5)
#define FLASH_SR_WRPRTERR (1U << 4)
#define FLASH_SR_PGERR  (1U << 2)
#define FLASH_CR_PG     (1U << 0)
#define FLASH_CR_PER    (1U << 1)
#define FLASH_CR_STRT   (1U << 6)
#define FLASH_CR_LOCK   (1U << 7)

/* ---- CRC32 (IEEE 802.3) ---- */
static uint32_t _crc32_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (int i = 0; i < 8; i++)
        crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
    return crc;
}

uint32_t FlashCfg_CRC32(const FlashCfg_t *cfg)
{
    uint32_t crc = 0xFFFFFFFFUL;
    const uint8_t *p = (const uint8_t *)cfg;
    /* 计算范围：magic + sn + hw_ver，不含 crc32 字段 */
    size_t len = sizeof(FlashCfg_t) - sizeof(uint32_t);
    for (size_t i = 0; i < len; i++)
        crc = _crc32_byte(crc, p[i]);
    return crc ^ 0xFFFFFFFFUL;
}

/* ---- Flash 解锁/上锁 ---- */
static void _flash_unlock(void)
{
    FLASH_REG->KEYR = FLASH_KEY1;
    FLASH_REG->KEYR = FLASH_KEY2;
}

static void _flash_lock(void)
{
    FLASH_REG->CR |= FLASH_CR_LOCK;
}

static void _flash_wait(void)
{
    while (FLASH_REG->SR & FLASH_SR_BSY);
}

/* ---- 擦除页 ---- */
static bool _flash_erase_page(uint32_t addr)
{
    _flash_wait();
    FLASH_REG->SR  = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
    FLASH_REG->CR |= FLASH_CR_PER;
    FLASH_REG->AR  = addr;
    FLASH_REG->CR |= FLASH_CR_STRT;
    _flash_wait();
    FLASH_REG->CR &= ~FLASH_CR_PER;
    return !(FLASH_REG->SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR));
}

/* ---- 写半字（16bit，Flash 最小写单位）---- */
static bool _flash_write_hword(uint32_t addr, uint16_t data)
{
    _flash_wait();
    FLASH_REG->CR |= FLASH_CR_PG;
    *(volatile uint16_t *)addr = data;
    _flash_wait();
    FLASH_REG->CR &= ~FLASH_CR_PG;
    return *(volatile uint16_t *)addr == data;
}

/* ---- 公开 API ---- */

void FlashCfg_Default(FlashCfg_t *cfg)
{
    memset(cfg, 0, sizeof(FlashCfg_t));
    cfg->magic    = FLASH_CFG_MAGIC;
    strncpy(cfg->sn,  "SHY-0000-000000", FLASH_SN_LEN - 1);
    strncpy(cfg->uid, "0000000000000000", FLASH_UID_LEN);
    cfg->hw_ver[0] = 1;
    cfg->hw_ver[1] = 0;
    cfg->hw_ver[2] = 0;
    cfg->hw_ver[3] = 0;
    cfg->crc32 = FlashCfg_CRC32(cfg);
}

bool FlashCfg_ValidateUID(const char *uid)
{
    if (!uid) return false;
    if (strlen(uid) != 16) return false;
    for (int i = 0; i < 16; i++) {
        char c = uid[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'A' && c <= 'F') ||
              (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

bool FlashCfg_Load(FlashCfg_t *cfg)
{
    memcpy(cfg, (const void *)FLASH_CFG_ADDR, sizeof(FlashCfg_t));

    if (cfg->magic != FLASH_CFG_MAGIC) return false;

    uint32_t crc = FlashCfg_CRC32(cfg);
    return crc == cfg->crc32;
}

bool FlashCfg_Save(const FlashCfg_t *cfg)
{
    FlashCfg_t tmp;
    memcpy(&tmp, cfg, sizeof(FlashCfg_t));
    tmp.crc32 = FlashCfg_CRC32(&tmp);

    _flash_unlock();

    if (!_flash_erase_page(FLASH_CFG_ADDR)) {
        _flash_lock();
        return false;
    }

    /* 按半字写入 */
    const uint16_t *src = (const uint16_t *)&tmp;
    uint32_t addr = FLASH_CFG_ADDR;
    for (size_t i = 0; i < sizeof(FlashCfg_t) / 2; i++) {
        if (!_flash_write_hword(addr, src[i])) {
            _flash_lock();
            return false;
        }
        addr += 2;
    }

    _flash_lock();

    /* 回读验证 */
    FlashCfg_t verify;
    memcpy(&verify, (const void *)FLASH_CFG_ADDR, sizeof(FlashCfg_t));
    return memcmp(&tmp, &verify, sizeof(FlashCfg_t)) == 0;
}
