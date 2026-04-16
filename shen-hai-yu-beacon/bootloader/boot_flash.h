#ifndef __BOOT_FLASH_H__
#define __BOOT_FLASH_H__

#include <stdint.h>
#include <stdbool.h>

#define APP_ADDR            0x08004000UL
#define FLASH_CFG_ADDR      0x0801FC00UL
#define FLASH_CFG_MAGIC     0xBEAC0001UL
#define FLASH_PAGE_SIZE     1024U

/* Flash 配置区结构（与 App 侧 flash_cfg.h 保持一致） */
#pragma pack(1)
typedef struct {
    uint32_t magic;
    char     sn[24];
    uint8_t  hw_ver[4];
    char     uid[17];
    uint8_t  rsvd[3];
    uint32_t crc32;
} BootFlashCfg_t;
#pragma pack()

bool BootFlash_ErasePage(uint32_t addr);
bool BootFlash_WriteHWord(uint32_t addr, uint16_t data);
bool BootFlash_WriteBlock(uint32_t addr, const uint8_t *data, uint32_t len);

/* 下载完成后更新配置区的软件版本号 */
bool BootFlash_UpdateFwVersion(uint8_t major, uint8_t minor, uint8_t patch);

/* CRC32 */
uint32_t BootFlash_CRC32(const uint8_t *buf, uint32_t len);

#endif
