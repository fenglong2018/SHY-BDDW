"""
[已废弃 / DEPRECATED]

此文件为项目初期版本的协议解析库，配合 beacon_tool.py 使用，现已废弃。

废弃原因：
  - 固件 GNSS 协议已从 NMEA 改为 SDBP 二进制，parse_gga/parse_rmc 不再适用
  - parse_debug_log 的正则匹配基于旧版日志格式，当前固件日志格式已变更
  - bat_mv_to_percent 已被固件侧 OCV 查表替代，线性计算不准确

请使用新工具：host_tool/ 目录下的图形界面工具。

保留此文件仅供参考。
-------------------------------------------------------------------
沈海渔示位标 通信协议解析（旧版）
"""

import re
from dataclasses import dataclass, field
from typing import Optional
from datetime import datetime


@dataclass
class GnssData:
    valid: bool = False
    latitude: float = 0.0
    longitude: float = 0.0
    altitude: float = 0.0
    speed: float = 0.0
    satellites: int = 0
    fix_quality: int = 0
    utc_time: str = ""
    utc_date: str = ""


@dataclass
class BeaconStatus:
    state: str = "UNKNOWN"
    bat_mv: int = 0
    bat_pct: int = 0
    usb_in: bool = False
    gnss: GnssData = field(default_factory=GnssData)
    module_id: str = ""
    fw_version: str = ""
    timestamp: datetime = field(default_factory=datetime.now)


def parse_nmea_coord(raw: str, direction: str) -> float:
    """解析 NMEA 度分格式坐标"""
    if not raw:
        return 0.0
    raw_f = float(raw)
    deg = int(raw_f / 100)
    minutes = raw_f - deg * 100
    result = deg + minutes / 60.0
    if direction in ('S', 'W'):
        result = -result
    return result


def nmea_checksum(sentence: str) -> int:
    cs = 0
    for c in sentence[1:]:
        if c == '*':
            break
        cs ^= ord(c)
    return cs


def parse_gga(sentence: str) -> Optional[GnssData]:
    """解析 $GNGGA / $GPGGA"""
    # 校验和验证
    if '*' in sentence:
        body, cs_str = sentence.rsplit('*', 1)
        expected = int(cs_str.strip(), 16)
        if nmea_checksum(body) != expected:
            return None

    parts = sentence.split(',')
    if len(parts) < 10:
        return None

    fix_q = int(parts[6]) if parts[6] else 0
    if fix_q == 0:
        return GnssData(valid=False)

    return GnssData(
        valid=True,
        utc_time=parts[1],
        latitude=parse_nmea_coord(parts[2], parts[3]),
        longitude=parse_nmea_coord(parts[4], parts[5]),
        fix_quality=fix_q,
        satellites=int(parts[7]) if parts[7] else 0,
        altitude=float(parts[9]) if parts[9] else 0.0,
    )


def parse_rmc(sentence: str) -> Optional[dict]:
    """解析 $GNRMC / $GPRMC，返回速度和日期"""
    parts = sentence.split(',')
    if len(parts) < 10 or parts[2] != 'A':
        return None
    return {
        'speed': float(parts[7]) if parts[7] else 0.0,
        'date': parts[9],
    }


def parse_debug_log(line: str) -> Optional[dict]:
    """解析固件调试日志"""
    patterns = {
        'state':   r'\[LOG\] State: (\w+)',
        'bat':     r'\[LOG\] Battery: (\d+)mV \((\d+)%\)',
        'pos':     r'\[LOG\] Position sent: ([-\d.]+), ([-\d.]+)',
        'sos':     r'\[LOG\] !!! SOS TRIGGERED !!!',
        'version': r'=== (.+) (V[\d.]+) ===',
    }
    for key, pattern in patterns.items():
        m = re.search(pattern, line)
        if m:
            return {'type': key, 'match': m}
    return None


def bat_mv_to_percent(mv: int) -> int:
    """电池电压转百分比"""
    if mv >= 4200:
        return 100
    if mv <= 3300:
        return 0
    return int((mv - 3300) * 100 / 900)
