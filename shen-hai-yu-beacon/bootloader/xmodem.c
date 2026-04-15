#include "xmodem.h"
#include "boot_usb.h"
#include "boot_flash.h"
#include "boot_hw.h"
#include <string.h>

/* ---- CRC-16/CCITT ---- */
static uint16_t _crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0;
    while (len--) {
        crc ^= (uint16_t)(*buf++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

/* ---- 等待单字节，超时返回 -1 ---- */
static int _recv_byte(uint32_t timeout_ms)
{
    uint32_t t0 = Boot_GetTick();
    uint8_t  b;
    while ((Boot_GetTick() - t0) < timeout_ms) {
        if (Boot_USB_ReadByte(&b)) return (int)b;
    }
    return -1;
}

/* ---- 发送控制字符 ---- */
static void _send(uint8_t c) { Boot_USB_SendByte(c); }

/* ---- 解析固件版本（从 App 向量表后的版本标记）----
 * 约定：App 在 0x08004200 处放置版本魔数
 * 格式：uint32_t magic=0x56455253, uint8_t major, minor, patch, rsvd
 */
#define FW_VER_MAGIC    0x56455253UL  /* "VERS" */
#define FW_VER_OFFSET   0x200U        /* App起始偏移0x200处 */

static void _parse_fw_version(uint32_t flash_addr,
                               uint8_t *major, uint8_t *minor, uint8_t *patch)
{
    *major = 1; *minor = 0; *patch = 0;  /* 默认值 */
    uint32_t ver_addr = flash_addr + FW_VER_OFFSET;
    uint32_t magic = *(volatile uint32_t *)ver_addr;
    if (magic == FW_VER_MAGIC) {
        *major = *(volatile uint8_t *)(ver_addr + 4);
        *minor = *(volatile uint8_t *)(ver_addr + 5);
        *patch = *(volatile uint8_t *)(ver_addr + 6);
    }
}

/* ---- XMODEM-CRC 接收主函数 ---- */
XmodemResult_t Xmodem_Receive(uint32_t flash_addr, uint32_t max_size)
{
    XmodemResult_t result = { XMODEM_ERR_TIMEOUT, 0, 1, 0, 0 };

    uint8_t  packet[XMODEM_PACKET_SIZE + 4]; /* SOH + SEQ + ~SEQ + DATA[128] + CRC[2] */
    uint8_t  expected_seq = 1;
    uint32_t write_addr   = flash_addr;
    uint8_t  retry        = 0;
    bool     first_packet = true;

    /* 发送 'C' 请求 CRC 模式 */
    _send(XMODEM_C);

    while (1) {
        int hdr = _recv_byte(XMODEM_TIMEOUT_MS);

        if (hdr < 0) {
            /* 超时 */
            if (++retry >= XMODEM_MAX_RETRY) {
                _send(XMODEM_CAN); _send(XMODEM_CAN);
                result.status = XMODEM_ERR_TIMEOUT;
                return result;
            }
            _send(XMODEM_C);
            continue;
        }

        if (hdr == XMODEM_EOT) {
            /* 传输完成 */
            _send(XMODEM_ACK);
            result.status = XMODEM_OK;
            result.bytes_written = write_addr - flash_addr;
            _parse_fw_version(flash_addr,
                              &result.fw_major,
                              &result.fw_minor,
                              &result.fw_patch);
            char buf[64];
            Boot_Snprintf(buf, sizeof(buf),
                "Received %lu bytes, FW V%d.%d.%d\r\n",
                (unsigned long)result.bytes_written,
                result.fw_major, result.fw_minor, result.fw_patch);
            Boot_USB_SendStr(buf);
            return result;
        }

        if (hdr == XMODEM_CAN) {
            int c2 = _recv_byte(1000);
            if (c2 == XMODEM_CAN) {
                _send(XMODEM_ACK);
                result.status = XMODEM_ERR_CANCELLED;
                return result;
            }
        }

        if (hdr != XMODEM_SOH) {
            _send(XMODEM_NAK);
            continue;
        }

        /* 读取序号 + 反序号 + 数据 + CRC */
        int seq  = _recv_byte(1000);
        int nseq = _recv_byte(1000);
        if (seq < 0 || nseq < 0) { _send(XMODEM_NAK); continue; }

        for (int i = 0; i < XMODEM_PACKET_SIZE; i++) {
            int b = _recv_byte(1000);
            if (b < 0) { _send(XMODEM_NAK); goto next; }
            packet[i] = (uint8_t)b;
        }
        {
            int ch = _recv_byte(1000);
            int cl = _recv_byte(1000);
            if (ch < 0 || cl < 0) { _send(XMODEM_NAK); goto next; }
            uint16_t recv_crc = (uint16_t)((ch << 8) | cl);
            uint16_t calc_crc = _crc16(packet, XMODEM_PACKET_SIZE);
            if (recv_crc != calc_crc) {
                Boot_USB_SendStr("CRC error\r\n");
                _send(XMODEM_NAK);
                goto next;
            }
        }

        /* 序号校验 */
        if ((uint8_t)seq != expected_seq ||
            (uint8_t)nseq != (uint8_t)(~expected_seq)) {
            /* 重复包：直接 ACK */
            if ((uint8_t)seq == (uint8_t)(expected_seq - 1)) {
                _send(XMODEM_ACK);
            } else {
                result.status = XMODEM_ERR_SEQ;
                _send(XMODEM_CAN); _send(XMODEM_CAN);
                return result;
            }
            goto next;
        }

        /* 第一包：擦除 App 区所有页 */
        if (first_packet) {
            Boot_USB_SendStr("Erasing app flash...\r\n");
            for (uint32_t p = flash_addr; p < flash_addr + max_size;
                 p += FLASH_PAGE_SIZE) {
                if (!BootFlash_ErasePage(p)) {
                    result.status = XMODEM_ERR_FLASH;
                    _send(XMODEM_CAN); _send(XMODEM_CAN);
                    return result;
                }
            }
            Boot_USB_SendStr("Erase OK, writing...\r\n");
            first_packet = false;
        }

        /* 写入 Flash */
        if (write_addr + XMODEM_PACKET_SIZE <= flash_addr + max_size) {
            if (!BootFlash_WriteBlock(write_addr, packet, XMODEM_PACKET_SIZE)) {
                result.status = XMODEM_ERR_FLASH;
                _send(XMODEM_CAN); _send(XMODEM_CAN);
                return result;
            }
            write_addr += XMODEM_PACKET_SIZE;
        }

        expected_seq++;
        retry = 0;
        _send(XMODEM_ACK);

next:;
    }
}
