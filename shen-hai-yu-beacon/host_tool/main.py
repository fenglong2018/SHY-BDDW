#!/usr/bin/env python3
"""
沈海渔示位标 上位机工具
功能：设备配置 + 固件升级(XMODEM-CRC) + 实时状态监控
"""

import sys
from PyQt6.QtWidgets import QApplication
from PyQt6.QtGui import QIcon
from ui.main_window import MainWindow

def main():
    app = QApplication(sys.argv)
    app.setApplicationName("沈海渔示位标工具")
    app.setApplicationVersion("1.0.0")
    app.setStyle("Fusion")

    window = MainWindow()
    window.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
