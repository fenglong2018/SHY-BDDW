#!/usr/bin/env python3
"""
[已废弃 / DEPRECATED]

此文件为项目初期版本的命令行调试工具，现已被 host_tool/ 图形界面工具取代。

废弃原因：
  - 固件 GNSS 协议已从 NMEA 文本格式改为泰斗 SDBP 二进制协议（TD-SDBP V3.29）
  - 固件调试日志格式已更新，此处的正则匹配不再适用
  - 缺少设备配置（SN/UID/版本写入）和固件升级（XMODEM-CRC）功能

请使用新工具：
  host_tool/dist/ShenHaiYuTool.exe  （图形界面，双击运行）
  或
  cd host_tool && python main.py    （源码运行）

保留此文件仅供参考，不建议在当前固件版本上使用。
-------------------------------------------------------------------
沈海渔示位标 上位机调试工具（旧版，命令行）
用法: python beacon_tool.py --port COM3 --baud 115200
"""

import argparse
import serial
import threading
import time
import sys
from datetime import datetime
from protocol import parse_gga, parse_rmc, parse_debug_log, bat_mv_to_percent, BeaconStatus, GnssData


class BeaconTool:
    def __init__(self, port: str, baud: int = 115200):
        self.port   = port
        self.baud   = baud
        self.ser    = None
        self.status = BeaconStatus()
        self._running = False
        self._lock    = threading.Lock()

    def connect(self) -> bool:
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=1)
            self._running = True
            t = threading.Thread(target=self._read_loop, daemon=True)
            t.start()
            print(f"[OK] Connected to {self.port} @ {self.baud}")
            return True
        except serial.SerialException as e:
            print(f"[ERR] Cannot open {self.port}: {e}")
            return False

    def disconnect(self):
        self._running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        print("[OK] Disconnected")

    def _read_loop(self):
        buf = ""
        while self._running:
            try:
                data = self.ser.read(64).decode('utf-8', errors='ignore')
                buf += data
                while '\n' in buf:
                    line, buf = buf.split('\n', 1)
                    line = line.strip()
                    if line:
                        self._process_line(line)
            except Exception:
                pass

    def _process_line(self, line: str):
        ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]

        # NMEA 语句
        if line.startswith('$GNGGA') or line.startswith('$GPGGA'):
            gnss = parse_gga(line)
            if gnss:
                with self._lock:
                    self.status.gnss = gnss
                if gnss.valid:
                    print(f"[{ts}] GNSS  lat={gnss.latitude:.5f} lon={gnss.longitude:.5f} "
                          f"alt={gnss.altitude:.1f}m sat={gnss.satellites}")
            return

        if line.startswith('$GNRMC') or line.startswith('$GPRMC'):
            rmc = parse_rmc(line)
            if rmc:
                with self._lock:
                    self.status.gnss.speed = rmc['speed']
                    self.status.gnss.utc_date = rmc['date']
            return

        # 调试日志
        result = parse_debug_log(line)
        if result:
            t = result['type']
            m = result['match']
            if t == 'bat':
                mv, pct = int(m.group(1)), int(m.group(2))
                with self._lock:
                    self.status.bat_mv  = mv
                    self.status.bat_pct = pct
                print(f"[{ts}] BAT   {mv}mV ({pct}%)")
            elif t == 'pos':
                print(f"[{ts}] SENT  lat={m.group(1)} lon={m.group(2)}")
            elif t == 'sos':
                print(f"[{ts}] *** SOS TRIGGERED ***")
            elif t == 'version':
                with self._lock:
                    self.status.fw_version = m.group(2)
                print(f"[{ts}] FW    {m.group(1)} {m.group(2)}")
            return

        # 原始输出
        print(f"[{ts}] RAW   {line}")

    def get_status(self) -> BeaconStatus:
        with self._lock:
            return self.status

    def send_command(self, cmd: str):
        """发送 AT 指令（透传到短报文模块）"""
        if self.ser and self.ser.is_open:
            self.ser.write((cmd + '\r\n').encode())
            print(f"[TX] {cmd}")

    def monitor(self):
        """持续监控模式"""
        print("=" * 50)
        print("  沈海渔示位标 监控工具")
        print("  按 Ctrl+C 退出，输入 AT 指令后回车发送")
        print("=" * 50)

        import select, os
        while True:
            try:
                # Windows 下使用 input() 非阻塞读取
                line = input()
                if line.strip():
                    self.send_command(line.strip())
            except KeyboardInterrupt:
                break
            except EOFError:
                time.sleep(0.1)


def main():
    parser = argparse.ArgumentParser(description='沈海渔示位标调试工具')
    parser.add_argument('--port',  '-p', required=True, help='串口号，如 COM3 或 /dev/ttyUSB0')
    parser.add_argument('--baud',  '-b', type=int, default=115200, help='波特率 (默认 115200)')
    parser.add_argument('--log',   '-l', help='保存日志到文件')
    args = parser.parse_args()

    tool = BeaconTool(args.port, args.baud)

    if args.log:
        # 重定向输出到文件
        import io
        log_file = open(args.log, 'a', encoding='utf-8')
        sys.stdout = io.TextIOWrapper(
            io.BufferedWriter(log_file.buffer),
            encoding='utf-8', line_buffering=True
        )
        print(f"=== Log started at {datetime.now()} ===")

    if not tool.connect():
        sys.exit(1)

    try:
        tool.monitor()
    finally:
        tool.disconnect()
        if args.log:
            sys.stdout = sys.__stdout__


if __name__ == '__main__':
    main()
