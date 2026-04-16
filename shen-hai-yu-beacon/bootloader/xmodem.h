#ifndef __XMODEM_H__
#define __XMODEM_H__

#include <stdint.h>
#include <stdbool.h>

/* XMODEM-CRC 控制字符 */
#define XMODEM_SOH   0x01U   /* 128字节数据包头 */
#define XMODEM_EOT   0x04U   /* 传输结束 */
#define XMODEM_ACK   0x06U   /* 确认 */
#define XMODEM_NAK   0x15U   /* 否认/重传 */
#define XMODEM_CAN   0x18U   /* 取消 */
#define XMODEM_C     0x43U   /* 'C'，请求CRC模式 */

#define XMODEM_PACKET_SIZE  128U
#define XMODEM_TIMEOUT_MS   3000U
#define XMODEM_MAX_RETRY    10U

typedef enum {
    XMODEM_OK = 0,
    XMODEM_ERR_TIMEOUT,
    XMODEM_ERR_CANCELLED,
    XMODEM_ERR_FLASH,
    XMODEM_ERR_SEQ,
} XmodemStatus_t;

typedef struct {
    XmodemStatus_t status;
    uint32_t       bytes_written;
    uint8_t        fw_major;    /* 从固件头解析的版本号 */
    uint8_t        fw_minor;
    uint8_t        fw_patch;
} XmodemResult_t;

/**
 * 接收固件并写入 Flash
 * @param flash_addr  目标 Flash 地址（App 起始）
 * @param max_size    最大接收字节数
 */
XmodemResult_t Xmodem_Receive(uint32_t flash_addr, uint32_t max_size);

#endif
