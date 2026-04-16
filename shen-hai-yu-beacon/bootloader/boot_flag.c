#include "boot_flag.h"
#include "boot_flash.h"
#include <string.h>

#define SLOT_A_ADDR     0x08004000UL
#define SLOT_B_ADDR     0x08010000UL
#define SLOT_SIZE       (48U * 1024U)   /* 48KB */

uint32_t BootFlag_SlotAddr(uint8_t slot)
{
    return (slot == SLOT_A) ? SLOT_A_ADDR : SLOT_B_ADDR;
}

uint32_t BootFlag_SlotSize(void)
{
    return SLOT_SIZE;
}

void BootFlag_Default(BootFlag_t *flag)
{
    memset(flag, 0, sizeof(BootFlag_t));
    flag->magic         = BOOT_FLAG_MAGIC;
    flag->active_slot   = SLOT_A;
    flag->upgrade_slot  = SLOT_NONE;
    flag->upgrade_state = UPGRADE_IDLE;
    flag->boot_fail_cnt = 0;
    flag->crc32 = BootFlash_CRC32((const uint8_t *)flag,
                                   sizeof(BootFlag_t) - sizeof(uint32_t));
}

bool BootFlag_Load(BootFlag_t *flag)
{
    memcpy(flag, (const void *)BOOT_FLAG_ADDR, sizeof(BootFlag_t));
    if (flag->magic != BOOT_FLAG_MAGIC) return false;
    uint32_t crc = BootFlash_CRC32((const uint8_t *)flag,
                                    sizeof(BootFlag_t) - sizeof(uint32_t));
    return crc == flag->crc32;
}

bool BootFlag_Save(const BootFlag_t *flag)
{
    BootFlag_t tmp;
    memcpy(&tmp, flag, sizeof(BootFlag_t));
    tmp.crc32 = BootFlash_CRC32((const uint8_t *)&tmp,
                                 sizeof(BootFlag_t) - sizeof(uint32_t));

    /* 擦除状态区（2页）*/
    if (!BootFlash_ErasePage(BOOT_FLAG_ADDR)) return false;
    if (!BootFlash_ErasePage(BOOT_FLAG_ADDR + FLASH_PAGE_SIZE)) return false;

    /* 写入 */
    return BootFlash_WriteBlock(BOOT_FLAG_ADDR,
                                (const uint8_t *)&tmp, sizeof(BootFlag_t));
}
