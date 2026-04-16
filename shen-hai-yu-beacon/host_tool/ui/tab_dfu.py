"""
固件升级标签页（XMODEM-CRC）
"""
import os
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox,
    QLabel, QLineEdit, QPushButton, QProgressBar,
    QTextEdit, QFileDialog
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QFont
from core.serial_port import SerialPort
from core.xmodem import XmodemSender


class DfuThread(QThread):
    progress = pyqtSignal(int, int)   # sent, total
    log      = pyqtSignal(str)
    finished = pyqtSignal(bool)       # success

    def __init__(self, port: SerialPort, filepath: str):
        super().__init__()
        self._port = port
        self._filepath = filepath
        self._sender: XmodemSender | None = None

    def cancel(self):
        if self._sender:
            self._sender.cancel()

    def run(self):
        self._sender = XmodemSender(
            self._port,
            progress_cb=lambda s, t: self.progress.emit(s, t),
            log_cb=lambda m: self.log.emit(m)
        )
        # 暂停 RX 回调，让 XMODEM 直接读串口
        self._port.set_on_data(None)
        try:
            ok = self._sender.send_file(self._filepath)
            self.finished.emit(ok)
        except Exception as e:
            self.log.emit(f"异常: {e}")
            self.finished.emit(False)


class DfuTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._port: SerialPort | None = None
        self._thread: DfuThread | None = None
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(10)

        # ---- 说明 ----
        info = QLabel(
            "升级步骤：\n"
            "1. 在「设备配置」页点击「进入升级模式」，或上电时按住 PB7\n"
            "2. 重新连接串口（设备重启后 COM 口可能变化）\n"
            "3. 选择固件文件（.bin），点击「开始升级」\n"
            "4. 等待进度条完成，设备自动重启"
        )
        info.setStyleSheet("background: #e8f4fd; padding: 10px; border-radius: 4px; color: #333;")
        info.setWordWrap(True)
        layout.addWidget(info)

        # ---- 文件选择 ----
        file_group = QGroupBox("固件文件")
        file_layout = QHBoxLayout(file_group)
        self._file_edit = QLineEdit()
        self._file_edit.setPlaceholderText("选择 .bin 固件文件...")
        self._file_edit.setReadOnly(True)
        browse_btn = QPushButton("浏览...")
        browse_btn.setFixedWidth(70)
        browse_btn.clicked.connect(self._browse_file)
        file_layout.addWidget(self._file_edit)
        file_layout.addWidget(browse_btn)
        layout.addWidget(file_group)

        # ---- 进度 ----
        prog_group = QGroupBox("升级进度")
        prog_layout = QVBoxLayout(prog_group)
        self._progress = QProgressBar()
        self._progress.setRange(0, 100)
        self._progress.setValue(0)
        self._progress.setTextVisible(True)
        self._progress_label = QLabel("就绪")
        self._progress_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        prog_layout.addWidget(self._progress)
        prog_layout.addWidget(self._progress_label)
        layout.addWidget(prog_group)

        # ---- 按钮 ----
        btn_layout = QHBoxLayout()
        self._start_btn = QPushButton("▶ 开始升级")
        self._start_btn.setFixedHeight(40)
        self._start_btn.setStyleSheet("QPushButton { background: #2196F3; color: white; font-weight: bold; border-radius: 4px; }"
                                       "QPushButton:disabled { background: #ccc; }")
        self._start_btn.clicked.connect(self._start_dfu)
        self._start_btn.setEnabled(False)

        self._cancel_btn = QPushButton("✕ 取消")
        self._cancel_btn.setFixedHeight(40)
        self._cancel_btn.setFixedWidth(80)
        self._cancel_btn.clicked.connect(self._cancel_dfu)
        self._cancel_btn.setEnabled(False)

        btn_layout.addWidget(self._start_btn)
        btn_layout.addWidget(self._cancel_btn)
        layout.addLayout(btn_layout)

        # ---- 日志 ----
        log_group = QGroupBox("升级日志")
        log_layout = QVBoxLayout(log_group)
        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setFont(QFont("Consolas", 9))
        log_layout.addWidget(self._log)
        layout.addWidget(log_group)

    def on_connected(self, port: SerialPort):
        self._port = port
        self._start_btn.setEnabled(bool(self._file_edit.text()))

    def on_disconnected(self):
        self._port = None
        self._start_btn.setEnabled(False)

    def _browse_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择固件文件", "", "固件文件 (*.bin);;所有文件 (*)"
        )
        if path:
            self._file_edit.setText(path)
            size = os.path.getsize(path)
            self._log_msg(f"已选择: {os.path.basename(path)} ({size:,} 字节)")
            if self._port:
                self._start_btn.setEnabled(True)

    def _log_msg(self, msg: str):
        from PyQt6.QtCore import QDateTime
        ts = QDateTime.currentDateTime().toString("hh:mm:ss")
        self._log.append(f"[{ts}] {msg}")

    def _start_dfu(self):
        if not self._port or not self._file_edit.text():
            return
        self._progress.setValue(0)
        self._progress_label.setText("正在升级...")
        self._start_btn.setEnabled(False)
        self._cancel_btn.setEnabled(True)
        self._log_msg("开始 XMODEM-CRC 传输...")

        self._thread = DfuThread(self._port, self._file_edit.text())
        self._thread.progress.connect(self._on_progress)
        self._thread.log.connect(self._log_msg)
        self._thread.finished.connect(self._on_finished)
        self._thread.start()

    def _cancel_dfu(self):
        if self._thread:
            self._thread.cancel()
        self._log_msg("用户取消升级")

    def _on_progress(self, sent: int, total: int):
        pct = int(sent * 100 / total) if total > 0 else 0
        self._progress.setValue(pct)
        self._progress_label.setText(f"{sent} / {total} 包 ({pct}%)")

    def _on_finished(self, success: bool):
        self._start_btn.setEnabled(True)
        self._cancel_btn.setEnabled(False)
        if success:
            self._progress.setValue(100)
            self._progress_label.setText("✅ 升级成功！设备正在重启...")
            self._log_msg("升级完成，设备将自动重启并运行新固件")
        else:
            self._progress_label.setText("❌ 升级失败")
            self._log_msg("升级失败，请重试")
