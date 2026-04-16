"""
设备 JSON 命令接口
"""
import json
import time
import threading
from typing import Optional
from .serial_port import SerialPort


class DeviceAPI:
    def __init__(self, port: SerialPort):
        self._port = port
        self._resp_buf = ""
        self._resp_event = threading.Event()
        self._resp_data: Optional[dict] = None
        self._lock = threading.Lock()
        self._port.set_on_data(self._on_raw_data)

    def _on_raw_data(self, data: bytes):
        try:
            text = data.decode("utf-8", errors="ignore")
        except Exception:
            return
        self._resp_buf += text
        # 按行处理
        while "\n" in self._resp_buf:
            line, self._resp_buf = self._resp_buf.split("\n", 1)
            line = line.strip()
            if line.startswith("{"):
                try:
                    obj = json.loads(line)
                    with self._lock:
                        self._resp_data = obj
                    self._resp_event.set()
                except json.JSONDecodeError:
                    pass

    def _send_cmd(self, cmd: dict, timeout: float = 5.0) -> Optional[dict]:
        with self._lock:
            self._resp_data = None
        self._resp_event.clear()
        self._port.send_str(json.dumps(cmd, ensure_ascii=False) + "\n")
        if self._resp_event.wait(timeout):
            with self._lock:
                return self._resp_data
        return None

    def get_info(self) -> Optional[dict]:
        return self._send_cmd({"cmd": "get_info"})

    def get_ver(self) -> Optional[dict]:
        return self._send_cmd({"cmd": "get_ver"})

    def get_status(self) -> Optional[dict]:
        return self._send_cmd({"cmd": "get_status"})

    def get_sn(self) -> Optional[dict]:
        return self._send_cmd({"cmd": "get_sn"})

    def set_sn(self, sn: str) -> Optional[dict]:
        return self._send_cmd({"cmd": "set_sn", "sn": sn})

    def get_uid(self) -> Optional[dict]:
        return self._send_cmd({"cmd": "get_uid"})

    def set_uid(self, uid: str) -> Optional[dict]:
        return self._send_cmd({"cmd": "set_uid", "uid": uid})

    def set_hw_ver(self, ver: str) -> Optional[dict]:
        return self._send_cmd({"cmd": "set_hw_ver", "hw_ver": ver})

    def get_bdid(self) -> Optional[dict]:
        return self._send_cmd({"cmd": "get_bdid"})

    def enter_dfu(self) -> Optional[dict]:
        return self._send_cmd({"cmd": "enter_dfu"}, timeout=3.0)
