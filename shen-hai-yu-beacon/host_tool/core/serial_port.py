"""
串口管理模块
"""
import serial
import serial.tools.list_ports
import threading
import time
from typing import Callable, Optional


class SerialPort:
    def __init__(self):
        self._ser: Optional[serial.Serial] = None
        self._rx_thread: Optional[threading.Thread] = None
        self._running = False
        self._on_data: Optional[Callable[[bytes], None]] = None
        self._lock = threading.Lock()

    @staticmethod
    def list_ports() -> list[str]:
        ports = serial.tools.list_ports.comports()
        return [p.device for p in sorted(ports)]

    def connect(self, port: str, baud: int = 115200) -> bool:
        try:
            self._ser = serial.Serial(port, baud, timeout=0.1)
            self._running = True
            self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
            self._rx_thread.start()
            return True
        except serial.SerialException as e:
            self._ser = None
            raise ConnectionError(str(e))

    def disconnect(self):
        self._running = False
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    def is_connected(self) -> bool:
        return self._ser is not None and self._ser.is_open

    def send(self, data: bytes):
        with self._lock:
            if self._ser and self._ser.is_open:
                self._ser.write(data)

    def send_str(self, s: str):
        self.send(s.encode("utf-8"))

    def set_on_data(self, cb: Callable[[bytes], None]):
        self._on_data = cb

    def _rx_loop(self):
        buf = b""
        while self._running:
            try:
                if self._ser and self._ser.in_waiting:
                    data = self._ser.read(self._ser.in_waiting)
                    if data and self._on_data:
                        self._on_data(data)
                else:
                    time.sleep(0.01)
            except Exception:
                break

    def read_raw(self, size: int, timeout: float = 3.0) -> bytes:
        """阻塞读取指定字节数（用于 XMODEM）"""
        buf = b""
        deadline = time.time() + timeout
        while len(buf) < size and time.time() < deadline:
            if self._ser and self._ser.in_waiting:
                buf += self._ser.read(min(size - len(buf), self._ser.in_waiting))
            else:
                time.sleep(0.005)
        return buf

    def read_byte_timeout(self, timeout: float = 3.0) -> Optional[int]:
        """读取单字节，超时返回 None（用于 XMODEM）"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self._ser and self._ser.in_waiting:
                b = self._ser.read(1)
                return b[0] if b else None
            time.sleep(0.005)
        return None
