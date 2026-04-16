"""
状态监控标签页
"""
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QLabel, QPushButton, QTextEdit
)
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QFont, QColor
from core.device_api import DeviceAPI


PHASE_NAMES = {
    "IDLE":    ("待机休眠", "#888"),
    "0_1H":    ("救援 0~1h", "#2196F3"),
    "1_3H":    ("救援 1~3h", "#2196F3"),
    "3_72H":   ("救援 3~72h", "#2196F3"),
    "EXPIRED": ("已结束", "#888"),
    "TEST":    ("测试模式", "#FF9800"),
    "UNKNOWN": ("未知", "#888"),
}


class StatusCard(QLabel):
    def __init__(self, title: str, parent=None):
        super().__init__(parent)
        self._title = title
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setMinimumSize(140, 70)
        self._set_value("—", "#888")

    def _set_value(self, value: str, color: str = "#333"):
        self.setText(f'<div style="font-size:11px;color:#666">{self._title}</div>'
                     f'<div style="font-size:18px;font-weight:bold;color:{color}">{value}</div>')
        self.setStyleSheet(f"background: white; border: 1px solid #ddd; border-radius: 6px; padding: 4px;")

    def update_value(self, value: str, color: str = "#333"):
        self._set_value(value, color)


class MonitorTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._api: DeviceAPI | None = None
        self._timer = QTimer()
        self._timer.timeout.connect(self._refresh)
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(10)

        # ---- 状态卡片 ----
        cards_group = QGroupBox("实时状态")
        cards_layout = QHBoxLayout(cards_group)

        self._card_phase  = StatusCard("运行阶段")
        self._card_bat    = StatusCard("电池电量")
        self._card_gnss   = StatusCard("GNSS 定位")
        self._card_sos    = StatusCard("SOS 状态")
        self._card_reports= StatusCard("累计发送")

        for card in [self._card_phase, self._card_bat, self._card_gnss,
                     self._card_sos, self._card_reports]:
            cards_layout.addWidget(card)
        layout.addWidget(cards_group)

        # ---- 位置信息 ----
        pos_group = QGroupBox("最新位置")
        pos_grid = QGridLayout(pos_group)
        self._lat_lbl = QLabel("—")
        self._lon_lbl = QLabel("—")
        self._lat_lbl.setFont(QFont("Consolas", 11))
        self._lon_lbl.setFont(QFont("Consolas", 11))
        pos_grid.addWidget(QLabel("纬度:"), 0, 0, Qt.AlignmentFlag.AlignRight)
        pos_grid.addWidget(self._lat_lbl, 0, 1)
        pos_grid.addWidget(QLabel("经度:"), 0, 2, Qt.AlignmentFlag.AlignRight)
        pos_grid.addWidget(self._lon_lbl, 0, 3)
        pos_grid.setColumnStretch(1, 1)
        pos_grid.setColumnStretch(3, 1)
        layout.addWidget(pos_group)

        # ---- 控制 ----
        ctrl_layout = QHBoxLayout()
        self._auto_cb = QPushButton("⏸ 暂停自动刷新")
        self._auto_cb.setCheckable(True)
        self._auto_cb.toggled.connect(self._toggle_auto)
        self._refresh_btn = QPushButton("🔄 立即刷新")
        self._refresh_btn.clicked.connect(self._refresh)
        self._refresh_btn.setEnabled(False)
        self._auto_cb.setEnabled(False)
        ctrl_layout.addWidget(self._refresh_btn)
        ctrl_layout.addWidget(self._auto_cb)
        ctrl_layout.addStretch()
        layout.addLayout(ctrl_layout)

        # ---- 日志 ----
        log_group = QGroupBox("原始响应")
        log_layout = QVBoxLayout(log_group)
        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setFont(QFont("Consolas", 9))
        log_layout.addWidget(self._log)
        layout.addWidget(log_group)

    def on_connected(self, api: DeviceAPI):
        self._api = api
        self._refresh_btn.setEnabled(True)
        self._auto_cb.setEnabled(True)
        self._timer.start(3000)  # 每3秒自动刷新
        self._refresh()

    def on_disconnected(self):
        self._api = None
        self._timer.stop()
        self._refresh_btn.setEnabled(False)
        self._auto_cb.setEnabled(False)
        for card in [self._card_phase, self._card_bat, self._card_gnss,
                     self._card_sos, self._card_reports]:
            card.update_value("—")
        self._lat_lbl.setText("—")
        self._lon_lbl.setText("—")

    def _toggle_auto(self, paused: bool):
        if paused:
            self._timer.stop()
            self._auto_cb.setText("▶ 恢复自动刷新")
        else:
            self._timer.start(3000)
            self._auto_cb.setText("⏸ 暂停自动刷新")

    def _refresh(self):
        if not self._api:
            return
        data = self._api.get_status()
        if not data:
            return

        from PyQt6.QtCore import QDateTime
        ts = QDateTime.currentDateTime().toString("hh:mm:ss")
        self._log.append(f"[{ts}] {data}")

        phase = data.get("phase", "UNKNOWN")
        name, color = PHASE_NAMES.get(phase, ("未知", "#888"))
        self._card_phase.update_value(name, color)

        bat = data.get("bat_pct", 0)
        bat_color = "#4CAF50" if bat > 50 else ("#FF9800" if bat > 20 else "#f44336")
        self._card_bat.update_value(f"{bat}%", bat_color)

        gnss = data.get("gnss", False)
        self._card_gnss.update_value("✅ 有效" if gnss else "❌ 无效",
                                      "#4CAF50" if gnss else "#f44336")

        sos = data.get("sos", False)
        self._card_sos.update_value("🆘 激活" if sos else "正常",
                                     "#f44336" if sos else "#4CAF50")

        reports = data.get("reports", 0)
        self._card_reports.update_value(str(reports), "#2196F3")

        lat = data.get("lat", 0.0)
        lon = data.get("lon", 0.0)
        if gnss:
            self._lat_lbl.setText(f"{lat:.6f}°")
            self._lon_lbl.setText(f"{lon:.6f}°")
        else:
            self._lat_lbl.setText("无定位")
            self._lon_lbl.setText("无定位")
