/**
 * @file  thread_config.c
 * @brief USB 配置接口线程
 *
 * 通过 USB CDC 虚拟串口收发 JSON 命令，实现：
 *   - 读取/写入产品 SN
 *   - 读取固件版本、硬件版本
 *   - 读取北斗卡号（从 RDSS 模块查询）
 *   - 读取当前设备状态
 *
 * 命令格式（每条命令以 '\n' 结尾）：
 *   {"cmd":"get_info"}
 *   {"cmd":"get_sn"}
 *   {"cmd":"set_sn","sn":"SHY-2026-000001"}
 *   {"cmd":"get_bdid"}
 *   {"cmd":"get_status"}
 *
 * 响应格式：
 *   {"ok":true,"sn":"SHY-2026-000001",...}
 *   {"ok":false,"err":"invalid sn"}
 */

#include <rtthread.h>
#include <string.h>
#include <stdio.h>
#include "msg_def.h"
#include "debug_config.h"
#include "../RTThread/rtconfig.h"
#include "../Drivers/usb_cdc.h"
#include "../Drivers/flash_cfg.h"
#include "../Drivers/gpio.h"
#include "../Core/system.h"

#define TAG         "CFG"
#define EN          DBG_BEACON
#define CMD_BUF_LEN 128
#define RSP_BUF_LEN 256

/* ---- 北斗卡号缓存（由 thread_rdss 填充）---- */
static char s_bdid[32] = "";   /* 从 $BDICP 解析的用户地址 */

/* 供 thread_rdss 调用，更新北斗卡号 */
void Config_SetBdId(const char *id)
{
    strncpy(s_bdid, id, sizeof(s_bdid) - 1);
}

/* ---- 简易 JSON 解析：提取字符串字段值 ----
 * 在 json 中找 "key":"value"，将 value 写入 out（最多 out_len-1 字节）
 * 返回 true=找到
 */
static bool _json_get_str(const char *json, const char *key,
                          char *out, size_t out_len)
{
    char search[32];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return true;
}

/* ---- 处理单条命令，返回 JSON 响应字符串 ---- */
static void _handle_cmd(const char *cmd_buf, char *rsp, size_t rsp_len)
{
    char cmd[32] = "";
    _json_get_str(cmd_buf, "cmd", cmd, sizeof(cmd));

    /* ---- get_info: 返回所有基本信息 ---- */
    if (strcmp(cmd, "get_info") == 0) {
        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        uint16_t bat_mv  = g_status.bat_mv;
        uint8_t  bat_pct = g_status.bat_pct;
        rt_bool_t usb_in = g_status.usb_in;
        rt_mutex_release(mtx_status);

        snprintf(rsp, rsp_len,
            "{\"ok\":true,"
            "\"product\":\"%s\","
            "\"fw_ver\":\"%s\","
            "\"hw_ver\":\"%d.%d.%d\","
            "\"sn\":\"%s\","
            "\"uid\":\"%s\","
            "\"bdid\":\"%s\","
            "\"bat_mv\":%d,"
            "\"bat_pct\":%d,"
            "\"usb\":%s"
            "}\n",
            PRODUCT_NAME,
            FW_VERSION_STR,
            g_cfg.hw_ver[0], g_cfg.hw_ver[1], g_cfg.hw_ver[2],
            g_cfg.sn,
            g_cfg.uid,
            s_bdid[0] ? s_bdid : "unknown",
            bat_mv, bat_pct,
            usb_in ? "true" : "false");
        return;
    }

    /* ---- get_sn ---- */
    if (strcmp(cmd, "get_sn") == 0) {
        snprintf(rsp, rsp_len,
            "{\"ok\":true,\"sn\":\"%s\"}\n", g_cfg.sn);
        return;
    }

    /* ---- set_sn ---- */
    if (strcmp(cmd, "set_sn") == 0) {
        char new_sn[FLASH_SN_LEN] = "";
        if (!_json_get_str(cmd_buf, "sn", new_sn, sizeof(new_sn))) {
            snprintf(rsp, rsp_len,
                "{\"ok\":false,\"err\":\"missing sn field\"}\n");
            return;
        }
        if (strlen(new_sn) == 0 || strlen(new_sn) >= FLASH_SN_LEN) {
            snprintf(rsp, rsp_len,
                "{\"ok\":false,\"err\":\"sn length invalid (1~%d)\"}\n",
                (int)(FLASH_SN_LEN - 1));
            return;
        }
        strncpy(g_cfg.sn, new_sn, FLASH_SN_LEN - 1);
        if (FlashCfg_Save(&g_cfg)) {
            snprintf(rsp, rsp_len,
                "{\"ok\":true,\"sn\":\"%s\"}\n", g_cfg.sn);
            DINF(TAG, EN, "SN saved: %s", g_cfg.sn);
        } else {
            snprintf(rsp, rsp_len,
                "{\"ok\":false,\"err\":\"flash write failed\"}\n");
        }
        return;
    }

    /* ---- get_bdid: 返回北斗卡号 ---- */
    if (strcmp(cmd, "get_bdid") == 0) {
        snprintf(rsp, rsp_len,
            "{\"ok\":true,\"bdid\":\"%s\"}\n",
            s_bdid[0] ? s_bdid : "unknown");
        return;
    }

    /* ---- get_status: 返回运行状态 ---- */
    if (strcmp(cmd, "get_status") == 0) {
        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        BeaconPhase_t phase      = g_status.phase;
        rt_bool_t     sos_active = g_status.sos_active;
        rt_bool_t     gnss_valid = g_status.gnss_valid;
        float         lat        = g_status.latitude;
        float         lon        = g_status.longitude;
        uint32_t      reports    = g_status.total_reports;
        uint16_t      bat_mv     = g_status.bat_mv;
        uint8_t       bat_pct    = g_status.bat_pct;
        rt_mutex_release(mtx_status);

        const char *phase_str[] = {
            "IDLE", "0_1H", "1_3H", "3_72H",
            "EXPIRED", "TEST"
        };
        const char *ps = (phase < 6) ? phase_str[phase] : "UNKNOWN";

        snprintf(rsp, rsp_len,
            "{\"ok\":true,"
            "\"phase\":\"%s\","
            "\"sos\":%s,"
            "\"gnss\":%s,"
            "\"lat\":%.5f,"
            "\"lon\":%.5f,"
            "\"reports\":%lu,"
            "\"bat_mv\":%d,"
            "\"bat_pct\":%d"
            "}\n",
            ps,
            sos_active ? "true" : "false",
            gnss_valid ? "true" : "false",
            lat, lon,
            (unsigned long)reports,
            bat_mv, bat_pct);
        return;
    }

    /* ---- get_ver: 返回软件版本和硬件版本 ---- */
    if (strcmp(cmd, "get_ver") == 0) {
        snprintf(rsp, rsp_len,
            "{\"ok\":true,"
            "\"fw_ver\":\"%s\","
            "\"hw_ver\":\"%d.%d.%d\","
            "\"sn\":\"%s\","
            "\"uid\":\"%s\""
            "}\n",
            FW_VERSION_STR,
            g_cfg.hw_ver[0], g_cfg.hw_ver[1], g_cfg.hw_ver[2],
            g_cfg.sn,
            g_cfg.uid);
        return;
    }

    /* ---- get_uid ---- */
    if (strcmp(cmd, "get_uid") == 0) {
        snprintf(rsp, rsp_len,
            "{\"ok\":true,\"uid\":\"%s\"}\n", g_cfg.uid);
        return;
    }

    /* ---- set_uid: 写入16位唯一识别码 ---- */
    /* 格式: {"cmd":"set_uid","uid":"A1B2C3D4E5F60001"} */
    if (strcmp(cmd, "set_uid") == 0) {
        char new_uid[FLASH_UID_LEN] = "";
        if (!_json_get_str(cmd_buf, "uid", new_uid, sizeof(new_uid))) {
            snprintf(rsp, rsp_len,
                "{\"ok\":false,\"err\":\"missing uid field\"}\n");
            return;
        }
        /* 转大写 */
        for (int i = 0; new_uid[i]; i++)
            if (new_uid[i] >= 'a' && new_uid[i] <= 'f')
                new_uid[i] -= 32;

        if (!FlashCfg_ValidateUID(new_uid)) {
            snprintf(rsp, rsp_len,
                "{\"ok\":false,\"err\":\"uid must be 16 uppercase hex chars (0-9,A-F)\"}\n");
            return;
        }
        strncpy(g_cfg.uid, new_uid, FLASH_UID_LEN - 1);
        if (FlashCfg_Save(&g_cfg)) {
            snprintf(rsp, rsp_len,
                "{\"ok\":true,\"uid\":\"%s\"}\n", g_cfg.uid);
            DINF(TAG, EN, "UID saved: %s", g_cfg.uid);
        } else {
            snprintf(rsp, rsp_len,
                "{\"ok\":false,\"err\":\"flash write failed\"}\n");
        }
        return;
    }

    /* ---- set_hw_ver: 写入硬件版本到Flash ---- */
    /* 格式: {"cmd":"set_hw_ver","hw_ver":"1.0.0"} */
    if (strcmp(cmd, "set_hw_ver") == 0) {
        char ver_str[16] = "";
        if (!_json_get_str(cmd_buf, "hw_ver", ver_str, sizeof(ver_str))) {
            snprintf(rsp, rsp_len,
                "{\"ok\":false,\"err\":\"missing hw_ver field\"}\n");
            return;
        }
        /* 解析 "major.minor.patch" */
        unsigned int ma = 0, mi = 0, pa = 0;
        if (sscanf(ver_str, "%u.%u.%u", &ma, &mi, &pa) != 3 ||
            ma > 99 || mi > 99 || pa > 99) {
            snprintf(rsp, rsp_len,
                "{\"ok\":false,\"err\":\"format must be major.minor.patch (0~99)\"}\n");
            return;
        }
        g_cfg.hw_ver[0] = (uint8_t)ma;
        g_cfg.hw_ver[1] = (uint8_t)mi;
        g_cfg.hw_ver[2] = (uint8_t)pa;
        g_cfg.hw_ver[3] = 0;
        if (FlashCfg_Save(&g_cfg)) {
            snprintf(rsp, rsp_len,
                "{\"ok\":true,\"hw_ver\":\"%d.%d.%d\"}\n",
                g_cfg.hw_ver[0], g_cfg.hw_ver[1], g_cfg.hw_ver[2]);
            DINF(TAG, EN, "HW ver saved: %d.%d.%d",
                 g_cfg.hw_ver[0], g_cfg.hw_ver[1], g_cfg.hw_ver[2]);
        } else {
            snprintf(rsp, rsp_len,
                "{\"ok\":false,\"err\":\"flash write failed\"}\n");
        }
        return;
    }

    /* ---- enter_dfu: 请求进入 Bootloader DFU 模式 ---- */
    if (strcmp(cmd, "enter_dfu") == 0) {
        snprintf(rsp, rsp_len,
            "{\"ok\":true,\"msg\":\"entering DFU in 1s, reconnect USB...\"}\n");
        USB_CDC_SendStr(rsp);
        /* 先关闭外设，等待线程安全停止 */
        GNSS_POWER_OFF();
        RDSS_POWER_OFF();
        rt_thread_mdelay(500);  /* 给其他线程 500ms 完成当前操作 */
        /* 写标志到备份寄存器 */
        volatile uint32_t *APB1ENR = (volatile uint32_t *)0x4002101CUL;
        *APB1ENR |= (1U << 28) | (1U << 27);
        volatile uint32_t *PWR_CR = (volatile uint32_t *)0x40007000UL;
        *PWR_CR |= (1U << 8);
        __asm volatile ("cpsid i");  /* 关中断，确保复位前无其他操作 */
        *(volatile uint32_t *)0x40006C04UL = 0xDF000001UL;
        volatile uint32_t *AIRCR = (volatile uint32_t *)0xE000ED0CUL;
        *AIRCR = (0x5FAUL << 16) | (1U << 2);
        while (1);
    }

    /* ---- 未知命令 ---- */
    snprintf(rsp, rsp_len,
        "{\"ok\":false,\"err\":\"unknown cmd: %s\"}\n", cmd);
}

/* ---- 主线程 ---- */
static void thread_config_entry(void *param)
{
    static char cmd_buf[CMD_BUF_LEN];
    static char rsp_buf[RSP_BUF_LEN];
    static uint16_t cmd_idx = 0;
    uint8_t byte;

    rt_kprintf("[%s] thread started\n", TAG);

    /* 等待 USB 枚举完成 */
    rt_thread_mdelay(2000);

    /* 上电欢迎信息 */
    char welcome[RSP_BUF_LEN];
    snprintf(welcome, sizeof(welcome),
        "\r\n=== %s ===\r\n"
        "FW: %s | HW: %d.%d.%d | SN: %s | UID: %s\r\n"
        "Commands: get_info/get_ver/get_sn/set_sn/set_hw_ver/get_uid/set_uid/get_bdid/get_status\r\n"
        "Format: {\"cmd\":\"get_info\"}\\n\r\n",
        PRODUCT_NAME,
        FW_VERSION_STR,
        g_cfg.hw_ver[0], g_cfg.hw_ver[1], g_cfg.hw_ver[2],
        g_cfg.sn,
        g_cfg.uid);
    USB_CDC_SendStr(welcome);

    while (1) {
        /* 从 USB CDC 读取字节，按行处理 */
        while (USB_CDC_ReadByte(&byte)) {
            if (byte == '\r') continue;  /* 忽略 CR */

            if (byte == '\n' || cmd_idx >= CMD_BUF_LEN - 1) {
                cmd_buf[cmd_idx] = '\0';
                if (cmd_idx > 2) {
                    DDBG(TAG, EN, "RX: %s", cmd_buf);
                    _handle_cmd(cmd_buf, rsp_buf, sizeof(rsp_buf));
                    USB_CDC_SendStr(rsp_buf);
                    DDBG(TAG, EN, "TX: %s", rsp_buf);
                }
                cmd_idx = 0;
            } else {
                cmd_buf[cmd_idx++] = (char)byte;
            }
        }
        rt_thread_mdelay(20);
    }
}

static int config_thread_init(void)
{
    rt_thread_t tid = rt_thread_create(
        "config",
        thread_config_entry, RT_NULL,
        512,
        6, 20
    );
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);
    return 0;
}
INIT_APP_EXPORT(config_thread_init);
