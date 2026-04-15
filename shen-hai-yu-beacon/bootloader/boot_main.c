/**
 * @file  boot_main.c
 * @brief Bootloader — A/B 双分区防变砖
 *
 * Flash 布局：
 *   0x08000000 ~ 0x08003FFF   16KB  Bootloader
 *   0x08004000 ~ 0x0800FFFF   48KB  App Slot A
 *   0x08010000 ~ 0x0801BFFF   48KB  App Slot B
 *   0x0801C000 ~ 0x0801EFFF   12KB  保留
 *   0x0801F000 ~ 0x0801F7FF    2KB  Boot Flag（升级状态区）
 *   0x0801F800 ~ 0x0801FFFF    2KB  Flash 配置区（SN/UID/版本）
 *
 * 防变砖机制：
 *   1. 升级时写入备用槽（不覆盖当前运行槽）
 *   2. 写完后设置 upgrade_state = PENDING，切换 active_slot
 *   3. 新固件启动后 App 调用 BootFlag_ConfirmOK() 设置 UPGRADE_OK
 *   4. 若连续 3 次启动未确认（boot_fail_cnt >= 3），自动回滚到旧槽
 *
 * 进入 DFU 的方式：
 *   1. 上电按住 PB7
 *   2. App USB 命令 {"cmd":"enter_dfu"}（写备份寄存器后复位）
 *   3. App 区无有效固件
 */

#include "boot_hw.h"
#include "boot_flash.h"
#include "boot_flag.h"
#include "boot_usb.h"
#include "xmodem.h"
#include <string.h>

/* 备份寄存器 DFU 请求标志 */
#define BKP_DFU_FLAG_ADDR   0x40006C04UL  /* BKP_DR1 */
#define DFU_REQUEST_MAGIC   0xDF000001UL

/* 系统复位 */
#define SYSTEM_RESET() \
    do { \
        volatile uint32_t *_aircr = (volatile uint32_t *)0xE000ED0CUL; \
        *_aircr = (0x5FAUL << 16) | (1U << 2); \
        while(1); \
    } while(0)

/* ---- 检查指定槽是否有有效 App ---- */
static bool _slot_valid(uint8_t slot)
{
    uint32_t addr = BootFlag_SlotAddr(slot);
    uint32_t sp   = *(volatile uint32_t *)addr;
    return (sp >= 0x20000000UL && sp <= 0x20008000UL);
}

/* ---- 跳转到指定槽 ---- */
static void _jump_to_slot(uint8_t slot)
{
    uint32_t addr  = BootFlag_SlotAddr(slot);
    uint32_t sp    = *(volatile uint32_t *)addr;
    uint32_t entry = *(volatile uint32_t *)(addr + 4);

    __asm volatile ("cpsid i");

    volatile uint32_t *VTOR = (volatile uint32_t *)0xE000ED08UL;
    *VTOR = addr;

    __asm volatile (
        "msr msp, %0\n"
        "bx  %1\n"
        : : "r"(sp), "r"(entry)
    );
}

/* ---- 检查备份寄存器 DFU 请求 ---- */
static bool _dfu_requested(void)
{
    volatile uint32_t *APB1ENR = (volatile uint32_t *)0x4002101CUL;
    *APB1ENR |= (1U << 28) | (1U << 27);
    volatile uint32_t *PWR_CR = (volatile uint32_t *)0x40007000UL;
    *PWR_CR |= (1U << 8);

    uint32_t flag = *(volatile uint32_t *)BKP_DFU_FLAG_ADDR;
    if (flag == DFU_REQUEST_MAGIC) {
        *(volatile uint32_t *)BKP_DFU_FLAG_ADDR = 0;
        return true;
    }
    return false;
}

/* ---- 打印槽信息 ---- */
static void _print_slot_info(const BootFlag_t *flag)
{
    char buf[128];
    Boot_Snprintf(buf, sizeof(buf),
        "Slot A: %s V%d.%d.%d\r\n"
        "Slot B: %s V%d.%d.%d\r\n"
        "Active: Slot %c\r\n",
        _slot_valid(SLOT_A) ? "OK " : "---",
        flag->slot_a_ver[0], flag->slot_a_ver[1], flag->slot_a_ver[2],
        _slot_valid(SLOT_B) ? "OK " : "---",
        flag->slot_b_ver[0], flag->slot_b_ver[1], flag->slot_b_ver[2],
        flag->active_slot == SLOT_A ? 'A' : 'B');
    Boot_USB_SendStr(buf);
}

/* ---- DFU 主流程 ---- */
static void _run_dfu(BootFlag_t *flag)
{
    Boot_USB_Init();
    Boot_USB_SendStr("\r\n=== ShenHaiYu Beacon Bootloader " BOOT_FW_VER " ===\r\n");
    _print_slot_info(flag);
    Boot_USB_SendStr("\r\nSend firmware via XMODEM-CRC, or:\r\n");
    Boot_USB_SendStr("  'Q' = boot current app\r\n");
    Boot_USB_SendStr("  'R' = rollback to previous slot\r\n\r\n");

    uint32_t t0 = Boot_GetTick();
    static uint32_t last_tip = 0;

    while (1) {
        uint8_t c;
        if (Boot_USB_ReadByte(&c)) {
            t0 = Boot_GetTick();  /* 有输入则重置超时 */

            /* Q：直接启动当前 App */
            if (c == 'Q' || c == 'q') {
                Boot_USB_SendStr("Booting current app...\r\n");
                goto do_boot;
            }

            /* R：手动回滚到另一个槽 */
            if (c == 'R' || c == 'r') {
                uint8_t other = (flag->active_slot == SLOT_A) ? SLOT_B : SLOT_A;
                if (_slot_valid(other)) {
                    char buf[64];
                    Boot_Snprintf(buf, sizeof(buf),
                        "Rolling back to Slot %c...\r\n",
                        other == SLOT_A ? 'A' : 'B');
                    Boot_USB_SendStr(buf);
                    flag->active_slot   = other;
                    flag->upgrade_state = UPGRADE_FAILED;
                    flag->boot_fail_cnt = 0;
                    BootFlag_Save(flag);
                    Boot_DelayMs(200);
                    SYSTEM_RESET();
                } else {
                    Boot_USB_SendStr("Previous slot invalid, cannot rollback.\r\n");
                }
                continue;
            }

            /* C / NAK：开始 XMODEM 接收 */
            if (c == 'C' || c == 0x15) {
                /* 写入备用槽（不覆盖当前运行槽） */
                uint8_t  target = (flag->active_slot == SLOT_A) ? SLOT_B : SLOT_A;
                uint32_t target_addr = BootFlag_SlotAddr(target);
                char buf[64];
                Boot_Snprintf(buf, sizeof(buf),
                    "Writing to Slot %c (0x%08lX)...\r\n",
                    target == SLOT_A ? 'A' : 'B',
                    (unsigned long)target_addr);
                Boot_USB_SendStr(buf);

                XmodemResult_t res = Xmodem_Receive(target_addr,
                                                     BootFlag_SlotSize());
                if (res.status == XMODEM_OK) {
                    Boot_USB_SendStr("\r\nDownload OK!\r\n");

                    /* 更新槽版本信息 */
                    uint8_t *ver = (target == SLOT_A)
                                 ? flag->slot_a_ver : flag->slot_b_ver;
                    ver[0] = res.fw_major;
                    ver[1] = res.fw_minor;
                    ver[2] = res.fw_patch;

                    /* 切换到新槽，标记为 PENDING（等待 App 确认） */
                    flag->active_slot   = target;
                    flag->upgrade_slot  = target;
                    flag->upgrade_state = UPGRADE_PENDING;
                    flag->boot_fail_cnt = 0;
                    BootFlag_Save(flag);

                    /* 更新配置区软件版本 */
                    BootFlash_UpdateFwVersion(
                        res.fw_major, res.fw_minor, res.fw_patch);

                    Boot_Snprintf(buf, sizeof(buf),
                        "Switching to Slot %c V%d.%d.%d, rebooting...\r\n",
                        target == SLOT_A ? 'A' : 'B',
                        res.fw_major, res.fw_minor, res.fw_patch);
                    Boot_USB_SendStr(buf);
                    Boot_DelayMs(500);
                    SYSTEM_RESET();
                } else {
                    Boot_USB_SendStr("\r\nDownload FAILED! Current app unchanged.\r\n");
                    /* 下载失败：不修改 flag，当前槽不受影响 */
                    _print_slot_info(flag);
                }
                continue;
            }
        }

        /* 5s 超时自动启动 */
        if ((Boot_GetTick() - t0) >= 5000) {
            Boot_USB_SendStr("\r\nTimeout, booting...\r\n");
            goto do_boot;
        }

        /* 倒计时提示 */
        if ((Boot_GetTick() - last_tip) >= 1000) {
            char buf[32];
            uint32_t remain = 5 - (Boot_GetTick() - t0) / 1000;
            Boot_Snprintf(buf, sizeof(buf), "Auto boot in %lus...\r",
                          (unsigned long)remain);
            Boot_USB_SendStr(buf);
            last_tip = Boot_GetTick();
        }
    }

do_boot:
    if (_slot_valid(flag->active_slot)) {
        Boot_DelayMs(200);
        _jump_to_slot(flag->active_slot);
    } else {
        Boot_USB_SendStr("Active slot invalid! Trying other slot...\r\n");
        uint8_t other = (flag->active_slot == SLOT_A) ? SLOT_B : SLOT_A;
        if (_slot_valid(other)) {
            flag->active_slot = other;
            BootFlag_Save(flag);
            _jump_to_slot(other);
        } else {
            Boot_USB_SendStr("Both slots invalid! Please download firmware.\r\n");
            /* 两个槽都无效，强制等待下载 */
            while (1) {
                uint8_t c;
                if (Boot_USB_ReadByte(&c) && (c == 'C' || c == 0x15)) {
                    uint32_t addr = BootFlag_SlotAddr(SLOT_A);
                    XmodemResult_t res = Xmodem_Receive(addr,
                                                         BootFlag_SlotSize());
                    if (res.status == XMODEM_OK) {
                        flag->active_slot   = SLOT_A;
                        flag->upgrade_state = UPGRADE_PENDING;
                        flag->boot_fail_cnt = 0;
                        flag->slot_a_ver[0] = res.fw_major;
                        flag->slot_a_ver[1] = res.fw_minor;
                        flag->slot_a_ver[2] = res.fw_patch;
                        BootFlag_Save(flag);
                        BootFlash_UpdateFwVersion(
                            res.fw_major, res.fw_minor, res.fw_patch);
                        SYSTEM_RESET();
                    }
                }
            }
        }
    }
}

/* ---- 回滚检查：连续启动失败 3 次则切换到另一个槽 ---- */
static void _check_rollback(BootFlag_t *flag)
{
    if (flag->upgrade_state != UPGRADE_PENDING) return;

    /* 每次进 Bootloader 且状态为 PENDING，说明上次 App 未确认（崩溃/卡死） */
    flag->boot_fail_cnt++;

    if (flag->boot_fail_cnt >= 3) {
        /* 连续 3 次失败，回滚到另一个槽 */
        uint8_t fallback = (flag->active_slot == SLOT_A) ? SLOT_B : SLOT_A;
        char buf[80];

        if (_slot_valid(fallback)) {
            Boot_USB_Init();
            Boot_Snprintf(buf, sizeof(buf),
                "\r\n[ROLLBACK] New firmware failed %d times!\r\n"
                "Rolling back to Slot %c V%d.%d.%d...\r\n",
                flag->boot_fail_cnt,
                fallback == SLOT_A ? 'A' : 'B',
                fallback == SLOT_A ? flag->slot_a_ver[0] : flag->slot_b_ver[0],
                fallback == SLOT_A ? flag->slot_a_ver[1] : flag->slot_b_ver[1],
                fallback == SLOT_A ? flag->slot_a_ver[2] : flag->slot_b_ver[2]);
            Boot_USB_SendStr(buf);

            flag->active_slot   = fallback;
            flag->upgrade_state = UPGRADE_FAILED;
            flag->boot_fail_cnt = 0;
            BootFlag_Save(flag);

            /* 同步更新配置区版本号为回滚版本 */
            uint8_t *ver = (fallback == SLOT_A)
                         ? flag->slot_a_ver : flag->slot_b_ver;
            BootFlash_UpdateFwVersion(ver[0], ver[1], ver[2]);

            Boot_DelayMs(500);
            SYSTEM_RESET();
        } else {
            /* 回滚槽也无效，进 DFU */
            flag->upgrade_state = UPGRADE_FAILED;
            flag->boot_fail_cnt = 0;
            BootFlag_Save(flag);
        }
    } else {
        /* 还没到 3 次，更新计数继续尝试 */
        BootFlag_Save(flag);
    }
}

/* ---- 入口 ---- */
int main(void)
{
    Boot_HW_Init();

    /* 加载升级状态 */
    BootFlag_t flag;
    if (!BootFlag_Load(&flag)) {
        BootFlag_Default(&flag);
        BootFlag_Save(&flag);
    }

    /* 回滚检查（PENDING 状态下计数） */
    _check_rollback(&flag);

    /* 判断是否进入 DFU */
    bool enter_dfu = false;

    if (Boot_IsKeyPressed()) {
        Boot_DelayMs(50);
        if (Boot_IsKeyPressed()) enter_dfu = true;
    }
    if (_dfu_requested())                    enter_dfu = true;
    if (!_slot_valid(flag.active_slot))      enter_dfu = true;

    if (enter_dfu) {
        _run_dfu(&flag);
    } else {
        _jump_to_slot(flag.active_slot);
    }

    while (1);
}
