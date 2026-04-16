#include "rdss.h"
#include "uart.h"
#include "gpio.h"
#include <string.h>
#include <stdio.h>

/* SIM 卡检测引脚: RD_SIMCARD (通过 R146 2M 上拉到 VCC_3V3) */
/* 插卡时 SW 引脚拉低 */
#define SIM_DETECT_PORT     GPIOB
#define SIM_DETECT_PIN      3   /* 根据实际连接调整 */

static RdssStatus_t s_status = {0};
static char         s_at_resp[128];
static bool         s_powered = false;

static void _flush_resp(void)
{
    memset(s_at_resp, 0, sizeof(s_at_resp));
    UART_Flush(UART_CH2);
}

static bool _wait_response(const char *expect, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t  idx   = 0;
    uint8_t  byte;

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (UART_ReadByte(UART_CH2, &byte)) {
            if (idx < sizeof(s_at_resp) - 1)
                s_at_resp[idx++] = (char)byte;
            if (strstr(s_at_resp, expect)) return true;
        }
    }
    return false;
}

void RDSS_Init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    UART_Init(UART_CH2, 9600);
}

bool RDSS_CheckSimCard(void)
{
    /* 通过 R146 检测：未插卡时 SW 引脚通过 2M 上拉为高，插卡后拉低 */
    /* 此处读取 MCU 对应 GPIO，低电平 = 已插卡 */
    /* 实际引脚需根据 PCB 连接确认 */
    s_status.sim_present = true;  /* TODO: 读取实际 GPIO */
    return s_status.sim_present;
}

void RDSS_PowerOn(void)
{
    if (s_powered) return;
    if (!RDSS_CheckSimCard()) return;  /* 未插卡不上电 */

    RDSS_POWER_ON();
    PA_POWER_ON();
    s_powered = true;
    HAL_Delay(2000);  /* 等待模块启动 */

    /* 初始化 AT */
    _flush_resp();
    RDSS_SendAT("AT\r\n", "OK", 1000);
    RDSS_SendAT("AT+CGSN\r\n", "+CGSN:", 2000);
    /* 解析模块 ID */
    char *p = strstr(s_at_resp, "+CGSN:");
    if (p) strncpy(s_status.module_id, p + 7, sizeof(s_status.module_id) - 1);
}

void RDSS_PowerOff(void)
{
    RDSS_POWER_OFF();
    PA_POWER_OFF();
    s_powered = false;
}

RdssErr_t RDSS_SendAT(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    _flush_resp();
    UART_SendStr(UART_CH2, cmd);
    if (!_wait_response(expect, timeout_ms)) return RDSS_ERR_TIMEOUT;
    return RDSS_OK;
}

RdssErr_t RDSS_SendMessage(const char *dest_id, const char *msg)
{
    if (!s_powered) return RDSS_ERR_SEND_FAIL;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=%s,%d\r\n", dest_id, (int)strlen(msg));

    _flush_resp();
    UART_SendStr(UART_CH2, cmd);
    if (!_wait_response(">", 3000)) return RDSS_ERR_TIMEOUT;

    UART_SendStr(UART_CH2, msg);
    UART_SendByte(UART_CH2, 0x1A);  /* Ctrl+Z 发送 */

    if (!_wait_response("OK", RDSS_AT_TIMEOUT_MS)) return RDSS_ERR_SEND_FAIL;
    return RDSS_OK;
}

RdssErr_t RDSS_SendPosition(float lat, float lon)
{
    char msg[RDSS_MSG_MAX_LEN];
    snprintf(msg, sizeof(msg), "SOS:%.5f,%.5f", lat, lon);
    /* 目标 ID 使用救援中心 ID，实际部署时配置 */
    return RDSS_SendMessage("0000001", msg);
}

void RDSS_Process(void)
{
    /* 处理主动上报的短报文接收 */
    uint8_t byte;
    static char buf[128];
    static uint8_t idx = 0;

    while (UART_ReadByte(UART_CH2, &byte)) {
        if (idx < sizeof(buf) - 1) buf[idx++] = (char)byte;
        if (byte == '\n') {
            buf[idx] = '\0';
            /* 处理接收到的短报文 */
            if (strstr(buf, "+CMGR:")) {
                /* TODO: 解析并处理接收到的消息 */
            }
            idx = 0;
        }
    }
}

RdssStatus_t *RDSS_GetStatus(void)
{
    return &s_status;
}
