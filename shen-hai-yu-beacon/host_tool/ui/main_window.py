"""
主窗口
"""
from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QTabWidget, QStatusBar, QLabel, QComboBox,
    QPushButton, QGroupBox
)
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QFont

from core.serial_port import SerialPort
from core.device_api import DeviceAPI
from .tab_config import ConfigTab
from .tab_dfu import DfuTab
from .tab_monitor import MonitorTab


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("沈海渔示位标工具 V1.0")
        self.setMinimumSize(800, 600)
        self.resize(900, 680)

        self._port = SerialPort()
        self._api: DeviceAPI | None = None

        self._build_ui()
        self._refresh_ports()

        # 定时刷新端口列表
        self._port_timer = QTimer()
        self._port_timer.timeout.connect(self._refresh_ports)
        self._port_timer.start(2000)

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        # ---- 连接栏 ----
        conn_group = QGroupBox("串口连接")
        conn_layout = QHBoxLayout(conn_group)

        self._port_combo = QComboBox()
        self._port_combo.setMinimumWidth(120)
        self._baud_combo = QComboBox()
        self._baud_combo.addItems(["115200", "9600", "57600", "230400"])
        self._connect_btn = QPushButton("连接")
        self._connect_btn.setFixedWidth(80)
        self._connect_btn.clicked.connect(self._toggle_connect)

        self._refresh_btn = QPushButton("刷新")
        self._refresh_btn.setFixedWidth(60)
        self._refresh_btn.clicked.connect(self._refresh_ports)

        self._conn_status = QLabel("● 未连接")
        self._conn_status.setStyleSheet("color: gray; font-weight: bold;")

        conn_layout.addWidget(QLabel("端口:"))
        conn_layout.addWidget(self._port_combo)
        conn_layout.addWidget(self._refresh_btn)
        conn_layout.addWidget(QLabel("波特率:"))
        conn_layout.addWidget(self._baud_combo)
        conn_layout.addWidget(self._connect_btn)
        conn_layout.addStretch()
        conn_layout.addWidget(self._conn_status)
        layout.addWidget(conn_group)

        # ---- 标签页 ----
        self._tabs = QTabWidget()
        self._tab_config  = ConfigTab(self)
        self._tab_dfu     = DfuTab(self)
        self._tab_monitor = MonitorTab(self)

        self._tabs.addTab(self._tab_config,  "⚙ 设备配置")
        self._tabs.addTab(self._tab_dfu,     "⬆ 固件升级")
        self._tabs.addTab(self._tab_monitor, "📊 状态监控")
        layout.addWidget(self._tabs)

        # ---- 状态栏 ----
        self._status_bar = QStatusBar()
        self.setStatusBar(self._status_bar)
        self._status_bar.showMessage("就绪")

    def _refresh_ports(self):
        current = self._port_combo.currentText()
        ports = SerialPort.list_ports()
        self._port_combo.clear()
        self._port_combo.addItems(ports)
        if current in ports:
            self._port_combo.setCurrentText(current)

    def _toggle_connect(self):
        if self._port.is_connected():
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self._port_combo.currentText()
        baud = int(self._baud_combo.currentText())
        if not port:
            self.show_status("请选择串口")
            return
        try:
            self._port.connect(port, baud)
            self._api = DeviceAPI(self._port)
            self._connect_btn.setText("断开")
            self._conn_status.setText(f"● {port}")
            self._conn_status.setStyleSheet("color: green; font-weight: bold;")
            self.show_status(f"已连接 {port} @ {baud}")
            # 通知各标签页
            self._tab_config.on_connected(self._api)
            self._tab_dfu.on_connected(self._port)
            self._tab_monitor.on_connected(self._api)
        except Exception as e:
            self.show_status(f"连接失败: {e}")

    def _disconnect(self):
        self._tab_config.on_disconnected()
        self._tab_dfu.on_disconnected()
        self._tab_monitor.on_disconnected()
        self._port.disconnect()
        self._api = None
        self._connect_btn.setText("连接")
        self._conn_status.setText("● 未连接")
        self._conn_status.setStyleSheet("color: gray; font-weight: bold;")
        self.show_status("已断开")

    def show_status(self, msg: str):
        self._status_bar.showMessage(msg, 5000)

    def is_connected(self) -> bool:
        return self._port.is_connected()
