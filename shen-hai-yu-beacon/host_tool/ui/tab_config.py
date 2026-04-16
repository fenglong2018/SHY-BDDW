"""
设备配置标签页
"""
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QLabel, QLineEdit, QPushButton,
    QTextEdit, QFrame
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QFont
from core.device_api import DeviceAPI


class ReadThread(QThread):
    done = pyqtSignal(dict)
    error = pyqtSignal(str)

    def __init__(self, api: DeviceAPI):
        super().__init__()
        self._api = api

    def run(self):
        result = self._api.get_info()
        if result:
            self.done.emit(result)
        else:
            self.error.emit("读取超时，请检查连接")


class WriteThread(QThread):
    done = pyqtSignal(bool, str)

    def __init__(self, api: DeviceAPI, field: str, value: str):
        super().__init__()
        self._api = api
        self._field = field
        self._value = value

    def run(self):
        try:
            if self._field == "sn":
                r = self._api.set_sn(self._value)
            elif self._field == "uid":
                r = self._api.set_uid(self._value)
            elif self._field == "hw_ver":
                r = self._api.set_hw_ver(self._value)
            else:
                self.done.emit(False, "未知字段")
                return

            if r and r.get("ok"):
                self.done.emit(True, f"写入成功")
            else:
                err = r.get("err", "未知错误") if r else "无响应"
                self.done.emit(False, f"写入失败: {err}")
        except Exception as e:
            self.done.emit(False, str(e))


class ConfigTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._api: DeviceAPI | None = None
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(10)

        # ---- 只读信息 ----
        ro_group = QGroupBox("设备信息（只读）")
        ro_grid = QGridLayout(ro_group)
        ro_grid.setColumnStretch(1, 1)

        self._fields_ro = {}
        ro_items = [
            ("产品名称", "product"),
            ("固件版本", "fw_ver"),
            ("北斗卡号", "bdid"),
            ("电池电压", "bat_mv"),
            ("电量",     "bat_pct"),
        ]
        ro_grid.setColumnMinimumWidth(0, 100)
        ro_grid.setHorizontalSpacing(12)
        for row, (label, key) in enumerate(ro_items):
            lbl = QLabel(label + ":")
            lbl.setMinimumWidth(100)
            ro_grid.addWidget(lbl, row, 0, Qt.AlignmentFlag.AlignRight)
            val = QLabel("—")
            val.setMinimumWidth(200)
            val.setStyleSheet("color: #333; background: #f5f5f5; padding: 2px 6px; border-radius: 3px;")
            val.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            ro_grid.addWidget(val, row, 1)
            self._fields_ro[key] = val
        layout.addWidget(ro_group)

        # ---- 可写配置 ----
        rw_group = QGroupBox("设备配置（可读写）")
        rw_grid = QGridLayout(rw_group)
        rw_grid.setColumnStretch(1, 1)

        self._fields_rw = {}
        rw_items = [
            ("产品序列号 SN", "sn",     "SHY-2026-000001",    "set_sn"),
            ("唯一识别码 UID", "uid",   "A1B2C3D4E5F60001",   "set_uid"),
            ("硬件版本",       "hw_ver","1.0.0",              "set_hw_ver"),
        ]
        for row, (label, key, placeholder, _) in enumerate(rw_items):
            rw_grid.addWidget(QLabel(label + ":"), row, 0, Qt.AlignmentFlag.AlignRight)
            edit = QLineEdit()
            edit.setPlaceholderText(placeholder)
            rw_grid.addWidget(edit, row, 1)
            btn = QPushButton("写入")
            btn.setFixedWidth(60)
            btn.clicked.connect(lambda checked, k=key: self._write_field(k))
            rw_grid.addWidget(btn, row, 2)
            self._fields_rw[key] = edit
        layout.addWidget(rw_group)

        # ---- GPIO 控制 ----
        gpio_group = QGroupBox("GPIO 控制（电源使能）")
        gpio_grid = QGridLayout(gpio_group)
        gpio_grid.setColumnStretch(1, 1)

        self._gpio_btns = {}
        gpio_items = [
            ("PD14 - RDSS 模块使能 (EN_PRDSS)", "pd14"),
            ("PA2  - RDSS PA 5V 电源 (CVPOW5V)", "pa2"),
            ("PB6  - GNSS 模块使能 (EN_PGNSS)",  "pb6"),
        ]
        for row, (label, key) in enumerate(gpio_items):
            gpio_grid.addWidget(QLabel(label + ":"), row, 0, Qt.AlignmentFlag.AlignRight)
            state_lbl = QLabel("—")
            state_lbl.setStyleSheet("color: #333; background: #f5f5f5; padding: 2px 6px; border-radius: 3px; min-width: 40px;")
            gpio_grid.addWidget(state_lbl, row, 1)
            btn_on = QPushButton("开")
            btn_on.setFixedWidth(48)
            btn_on.setStyleSheet("QPushButton { color: #080; }")
            btn_on.clicked.connect(lambda checked, k=key: self._set_gpio(k, 1))
            btn_off = QPushButton("关")
            btn_off.setFixedWidth(48)
            btn_off.setStyleSheet("QPushButton { color: #c00; }")
            btn_off.clicked.connect(lambda checked, k=key: self._set_gpio(k, 0))
            btn_read = QPushButton("读取")
            btn_read.setFixedWidth(48)
            btn_read.clicked.connect(self._read_gpio)
            gpio_grid.addWidget(btn_on,   row, 2)
            gpio_grid.addWidget(btn_off,  row, 3)
            if row == 0:
                gpio_grid.addWidget(btn_read, row, 4)
            self._gpio_btns[key] = (state_lbl, btn_on, btn_off)
            btn_on.setEnabled(False)
            btn_off.setEnabled(False)
        layout.addWidget(gpio_group)

        # ---- 操作按钮 ----
        btn_layout = QHBoxLayout()
        self._read_btn = QPushButton("📖 读取所有信息")
        self._read_btn.setFixedHeight(36)
        self._read_btn.clicked.connect(self._read_all)
        self._read_btn.setEnabled(False)

        self._dfu_btn = QPushButton("🔄 进入升级模式 (DFU)")
        self._dfu_btn.setFixedHeight(36)
        self._dfu_btn.setStyleSheet("QPushButton { color: #c00; }")
        self._dfu_btn.clicked.connect(self._enter_dfu)
        self._dfu_btn.setEnabled(False)

        btn_layout.addWidget(self._read_btn)
        btn_layout.addStretch()
        btn_layout.addWidget(self._dfu_btn)
        layout.addLayout(btn_layout)

        # ---- 日志 ----
        log_group = QGroupBox("操作日志")
        log_layout = QVBoxLayout(log_group)
        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setMaximumHeight(120)
        self._log.setFont(QFont("Consolas", 9))
        log_layout.addWidget(self._log)
        layout.addWidget(log_group)
        layout.addStretch()

    def on_connected(self, api: DeviceAPI):
        self._api = api
        self._read_btn.setEnabled(True)
        self._dfu_btn.setEnabled(True)
        for edit in self._fields_rw.values():
            edit.setEnabled(True)
        for lbl, btn_on, btn_off in self._gpio_btns.values():
            btn_on.setEnabled(True)
            btn_off.setEnabled(True)
        self._log_msg("已连接，点击「读取所有信息」获取设备数据")
        self._read_all()
        self._read_gpio()

    def on_disconnected(self):
        self._api = None
        self._read_btn.setEnabled(False)
        self._dfu_btn.setEnabled(False)
        for edit in self._fields_rw.values():
            edit.setEnabled(False)
        for lbl in self._fields_ro.values():
            lbl.setText("—")
        for lbl, btn_on, btn_off in self._gpio_btns.values():
            lbl.setText("—")
            btn_on.setEnabled(False)
            btn_off.setEnabled(False)

    def _log_msg(self, msg: str):
        from PyQt6.QtCore import QDateTime
        ts = QDateTime.currentDateTime().toString("hh:mm:ss")
        self._log.append(f"[{ts}] {msg}")

    def _read_all(self):
        if not self._api:
            return
        self._read_btn.setEnabled(False)
        self._log_msg("正在读取...")
        self._thread = ReadThread(self._api)
        self._thread.done.connect(self._on_read_done)
        self._thread.error.connect(self._on_read_error)
        self._thread.start()

    def _on_read_done(self, data: dict):
        self._read_btn.setEnabled(True)
        # 只读字段
        self._fields_ro["product"].setText(data.get("product", "—"))
        self._fields_ro["fw_ver"].setText(data.get("fw_ver", "—"))
        self._fields_ro["bdid"].setText(data.get("bdid", "—"))
        mv = data.get("bat_mv", 0)
        pct = data.get("bat_pct", 0)
        self._fields_ro["bat_mv"].setText(f"{mv} mV")
        self._fields_ro["bat_pct"].setText(f"{pct} %")
        # 可写字段
        if "sn" in data:
            self._fields_rw["sn"].setText(data["sn"])
        if "uid" in data:
            self._fields_rw["uid"].setText(data["uid"])
        if "hw_ver" in data:
            self._fields_rw["hw_ver"].setText(data["hw_ver"])
        self._log_msg(f"读取成功：FW={data.get('fw_ver')} HW={data.get('hw_ver')} SN={data.get('sn')}")

    def _on_read_error(self, msg: str):
        self._read_btn.setEnabled(True)
        self._log_msg(f"错误：{msg}")

    def _write_field(self, key: str):
        if not self._api:
            return
        value = self._fields_rw[key].text().strip()
        if not value:
            self._log_msg(f"请输入 {key} 的值")
            return
        self._log_msg(f"写入 {key} = {value}...")
        self._wthread = WriteThread(self._api, key, value)
        self._wthread.done.connect(lambda ok, msg: self._log_msg(msg))
        self._wthread.start()

    def _enter_dfu(self):
        if not self._api:
            return
        from PyQt6.QtWidgets import QMessageBox
        ret = QMessageBox.question(
            self, "确认", "设备将重启进入固件升级模式，USB 会短暂断开。\n确认继续？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if ret == QMessageBox.StandardButton.Yes:
            self._log_msg("发送 enter_dfu 命令...")
            self._api.enter_dfu()
            self._log_msg("设备正在重启，请切换到「固件升级」标签页")

    def _read_gpio(self):
        if not self._api:
            return
        r = self._api.get_gpio()
        if r and r.get("ok"):
            self._update_gpio_labels(r)
        else:
            self._log_msg("GPIO 读取失败")

    def _update_gpio_labels(self, data: dict):
        for key in ("pd14", "pa2", "pb6"):
            if key in data and key in self._gpio_btns:
                lbl, _, _ = self._gpio_btns[key]
                val = data[key]
                if val:
                    lbl.setText("高 (开)")
                    lbl.setStyleSheet("color: #080; background: #e8f5e9; padding: 2px 6px; border-radius: 3px; min-width: 40px;")
                else:
                    lbl.setText("低 (关)")
                    lbl.setStyleSheet("color: #c00; background: #fdecea; padding: 2px 6px; border-radius: 3px; min-width: 40px;")

    def _set_gpio(self, key: str, val: int):
        if not self._api:
            return
        r = self._api.set_gpio(**{key: val})
        if r and r.get("ok"):
            self._update_gpio_labels(r)
            self._log_msg(f"GPIO {key.upper()} 已设置为 {'高(开)' if val else '低(关)'}")
        else:
            err = r.get("err", "无响应") if r else "无响应"
            self._log_msg(f"GPIO 设置失败: {err}")
