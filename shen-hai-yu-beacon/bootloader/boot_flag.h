#ifndef __BOOT_FLAG_H__
#define __BOOT_FLAG_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * 升级状态区（Boot Flag）
 * 地址：0x0801F000，大小 2KB（2页）
 *
 * 记录当前运行槽、升级状态、启动失败次数，
 * 用于实现 A/B 双分区防变砖回滚。
 */

#define BOOT_FLAG_ADDR      0x0801F000UL
#define BOOT_FLAG_MAGIC     0xB007F1A6UL  /* "BOOTFLAG" */

/* 槽定义 */
#define SLOT_A              0U   /* 0x08004000, 48KB */
#define SLOT_B              1U   /* 0x08010000, 48KB */
#define SLOT_NONE           0xFFU

/* 升级状态 */
typedef enum {
    UPGRADE_IDLE     = 0x00,  /* 无升级操作 */
    UPGRADE_PENDING  = 0xAA,  /* 新固件已写入，等待首次启动验证 */
    UPGRADE_OK       = 0x55,  /* 新固件启动验证通过 */
    UPGRADE_FAILED   = 0xFF,  /* 新固件启动失败，已回滚 */
} UpgradeState_t;

#pragma pack(1)
typedef struct {
    uint32_t       magic;
    uint8_t        active_slot;     /* 当前运行槽 SLOT_A / SLOT_B */
    uint8_t        upgrade_slot;    /* 升级目标槽 */
    uint8_t        upgrade_state;   /* UpgradeState_t */
    uint8_t        boot_fail_cnt;   /* 连续启动失败次数，>=3 触发回滚 */
    uint8_t        slot_a_ver[3];   /* Slot A 固件版本 major.minor.patch */
    uint8_t        slot_b_ver[3];   /* Slot B 固件版本 */
    uint8_t        rsvd[2];
    uint32_t       crc32;
} BootFlag_t;
#pragma pack()

/* API */
bool BootFlag_Load(BootFlag_t *flag);
bool BootFlag_Save(const BootFlag_t *flag);
void BootFlag_Default(BootFlag_t *flag);

/* 获取槽的 Flash 起始地址 */
uint32_t BootFlag_SlotAddr(uint8_t slot);
/* 获取槽的大小 */
uint32_t BootFlag_SlotSize(void);

#endif /* __BOOT_FLAG_H__ */
