#ifndef __BOOT_CONFIRM_H__
#define __BOOT_CONFIRM_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * App 启动确认
 * App 正常运行后调用此函数，通知 Bootloader 新固件有效。
 * 若不调用，Bootloader 连续 3 次启动失败后自动回滚。
 *
 * 建议在 main() 初始化完成、各线程启动后约 5s 调用。
 */
void BootConfirm_OK(void);

/**
 * 查询当前运行槽和升级状态（供 USB 配置接口使用）
 */
typedef struct {
    uint8_t active_slot;    /* 0=SlotA, 1=SlotB */
    uint8_t upgrade_state;  /* 0=IDLE, 0xAA=PENDING, 0x55=OK, 0xFF=FAILED */
    uint8_t slot_a_ver[3];
    uint8_t slot_b_ver[3];
} BootStatus_t;

void BootConfirm_GetStatus(BootStatus_t *status);

#endif /* __BOOT_CONFIRM_H__ */
