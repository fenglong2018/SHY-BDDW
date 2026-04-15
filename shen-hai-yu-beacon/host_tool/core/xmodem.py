"""
XMODEM-CRC 发送实现
"""
import time
from typing import Callable, Optional
from .serial_port import SerialPort

SOH  = 0x01
EOT  = 0x04
ACK  = 0x06
NAK  = 0x15
CAN  = 0x18
XMODEM_C = 0x43  # 'C'

PACKET_SIZE = 128
MAX_RETRY   = 10


def _crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = (crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1
    return crc & 0xFFFF


class XmodemSender:
    def __init__(self, port: SerialPort,
                 progress_cb: Optional[Callable[[int, int], None]] = None,
                 log_cb: Optional[Callable[[str], None]] = None):
        self._port = port
        self._progress_cb = progress_cb
        self._log_cb = log_cb
        self._cancelled = False

    def cancel(self):
        self._cancelled = True

    def _log(self, msg: str):
        if self._log_cb:
            self._log_cb(msg)

    def _wait_byte(self, timeout: float = 3.0) -> Optional[int]:
        return self._port.read_byte_timeout(timeout)

    def _send(self, data: bytes):
        self._port.send(data)

    def send_file(self, filepath: str) -> bool:
        """发送固件文件，返回 True=成功"""
        with open(filepath, "rb") as f:
            data = f.read()

        total = len(data)
        # 补齐到 128 字节倍数
        pad = (-total) % PACKET_SIZE
        data += b'\x1A' * pad
        total_packets = len(data) // PACKET_SIZE

        self._log(f"文件大小: {total} 字节，共 {total_packets} 包")
        self._log("等待设备发送 'C'...")

        # 等待设备发送 'C' 请求 CRC 模式
        retry = 0
        while retry < MAX_RETRY:
            b = self._wait_byte(3.0)
            if b == XMODEM_C:
                self._log("收到 'C'，开始发送...")
                break
            if b == NAK:
                self._log("收到 NAK，开始发送...")
                break
            retry += 1
        else:
            self._log("超时：未收到设备响应")
            return False

        seq = 1
        offset = 0
        sent = 0

        while offset < len(data):
            if self._cancelled:
                self._send(bytes([CAN, CAN]))
                self._log("用户取消")
                return False

            chunk = data[offset:offset + PACKET_SIZE]
            crc = _crc16(chunk)
            packet = bytes([SOH, seq & 0xFF, (~seq) & 0xFF]) + chunk + bytes([crc >> 8, crc & 0xFF])

            ack_ok = False
            for attempt in range(MAX_RETRY):
                self._send(packet)
                resp = self._wait_byte(3.0)
                if resp == ACK:
                    ack_ok = True
                    break
                elif resp == NAK:
                    self._log(f"包 {seq} NAK，重试 {attempt+1}")
                elif resp == CAN:
                    self._log("设备取消传输")
                    return False
                else:
                    self._log(f"包 {seq} 无响应，重试 {attempt+1}")

            if not ack_ok:
                self._log(f"包 {seq} 发送失败，放弃")
                self._send(bytes([CAN, CAN]))
                return False

            offset += PACKET_SIZE
            seq += 1
            sent += 1
            if self._progress_cb:
                self._progress_cb(sent, total_packets)

        # 发送 EOT
        for _ in range(MAX_RETRY):
            self._send(bytes([EOT]))
            resp = self._wait_byte(3.0)
            if resp == ACK:
                self._log("传输完成！")
                return True

        self._log("EOT 未收到 ACK")
        return False
