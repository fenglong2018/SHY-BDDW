#ifndef __FLASH_CFG_H__
#define __FLASH_CFG_H__

#include "system.h"

/**
 * Flash 配置区
 * N32L403KBQ7: Flash 128KB, 页大小 1KB
 * 配置区使用最后一页: 0x0801FC00 ~ 0x0801FFFF (1KB)
 *
 * 布局：
 *   [0x00] uint32_t magic       魔数 0xBEAC0001
 *   [0x04] char     sn[24]      产品序列号，最长23字符+'\0'
 *   [0x1C] uint8_t  hw_ver[4]   硬件版本 major.minor.patch.reserved
 *   [0x20] char     uid[17]     16位产品唯一识别码 + '\0'
 *                               格式：纯十六进制大写，如 "A1B2C3D4E5F60001"
 *   [0x31] uint8_t  rsvd[3]     对齐保留
 *   [0x34] uint32_t crc32       以上内容的CRC32校验
 */

#define FLASH_CFG_MAGIC     0xBEAC0001UL
#define FLASH_CFG_ADDR      0x0801FC00UL  /* 最后一页起始地址 */
#define FLASH_PAGE_SIZE     1024U
#define FLASH_SN_LEN        24U
#define FLASH_UID_LEN       17U           /* 16位HEX字符 + '\0' */

#pragma pack(1)
typedef struct {
    uint32_t magic;
    char     sn[FLASH_SN_LEN];    /* 产品SN，如 "SHY-2026-000001" */
    uint8_t  hw_ver[4];           /* 硬件版本 [major, minor, patch, rsvd] */
    char     uid[FLASH_UID_LEN];  /* 16位唯一识别码，如 "A1B2C3D4E5F60001" */
    uint8_t  rsvd[3];             /* 对齐保留 */
    uint32_t crc32;
} FlashCfg_t;
#pragma pack()

/* API */
bool     FlashCfg_Load(FlashCfg_t *cfg);
bool     FlashCfg_Save(const FlashCfg_t *cfg);
void     FlashCfg_Default(FlashCfg_t *cfg);
uint32_t FlashCfg_CRC32(const FlashCfg_t *cfg);

/* UID 格式校验：必须是16位大写十六进制字符 */
bool     FlashCfg_ValidateUID(const char *uid);

/* 全局配置实例（main.c 初始化） */
extern FlashCfg_t g_cfg;

#endif /* __FLASH_CFG_H__ */
