# 沈海渔示位标 上位机工具

## 功能

| 标签页 | 功能 |
|--------|------|
| ⚙ 设备配置 | 读写 SN / UID / 硬件版本，查看固件版本、北斗卡号、电量；GPIO 电源控制 |
| ⬆ 固件升级 | XMODEM-CRC 协议升级固件，实时进度显示 |
| 📊 状态监控 | 实时显示运行阶段、电量、GNSS定位、SOS状态、累计发送次数 |

---

## Windows

### 运行环境
- Python 3.10+
- Windows 10/11

### 安装依赖
```powershell
pip install PyQt6 pyserial pyinstaller
```

### 直接运行
```powershell
cd host_tool
python main.py
```

### 打包成 EXE
```powershell
cd host_tool
python build.py
# 生成 dist/ShenHaiYuTool.exe
```

---

## Ubuntu / Linux

### 运行环境
- Python 3.10+
- Ubuntu 20.04 / 22.04 / 24.04（或其他主流发行版）

### 安装系统依赖
```bash
sudo apt update
sudo apt install -y python3 python3-pip python3-venv \
    libxcb-cursor0 libxcb-xinerama0 libxcb-icccm4 \
    libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
    libxcb-render-util0 libxcb-shape0 libxkbcommon-x11-0
```

### 串口权限（重要）
Ubuntu 默认普通用户无权访问 `/dev/ttyUSB*` / `/dev/ttyACM*`，需加入 `dialout` 组：
```bash
sudo usermod -aG dialout $USER
# 重新登录后生效，或临时用：
newgrp dialout
```

### 安装 Python 依赖 & 打包（一步完成）

`build.py` 会自动创建虚拟环境并安装依赖，无需手动 pip：

```bash
cd host_tool
python3 build.py
# 自动创建 .venv、安装依赖、生成 dist/ShenHaiYuTool
```

运行打包后的程序：
```bash
./dist/ShenHaiYuTool
```

### 直接运行（开发模式）

```bash
cd host_tool
python3 -m venv .venv
source .venv/bin/activate
pip install PyQt6 pyserial
python3 main.py
```

---

## 使用说明

### 连接设备

1. 用 Type-C USB 连接设备
2. 在「端口」下拉框选择对应串口
   - Windows: `COM3` 等
   - Linux: `/dev/ttyACM0` 或 `/dev/ttyUSB0`
3. 波特率保持 115200
4. 点击「连接」

### 设备配置

连接后自动读取设备信息，可修改：

| 字段 | 格式 | 示例 |
|------|------|------|
| 产品序列号 SN | 任意字符串，最长23位 | SHY-2026-000001 |
| 唯一识别码 UID | 16位大写十六进制 | A1B2C3D4E5F60001 |
| 硬件版本 | major.minor.patch | 1.0.0 |

修改后点击对应「写入」按钮，数据保存到设备 Flash。

### GPIO 控制

连接后可单独控制三路电源引脚：

| 引脚 | 功能 |
|------|------|
| PD14 | RDSS 短报文模块使能 |
| PA2  | RDSS PA 5V 电源 |
| PB6  | GNSS 模块使能 |

点击「开」/「关」按钮即时生效，「读取」按钮刷新当前状态。

### 固件升级

**方式一：通过配置页触发**
1. 在「设备配置」页点击「进入升级模式」
2. 设备重启，USB 短暂断开
3. 重新连接串口
4. 切换到「固件升级」页，选择 `.bin` 文件
5. 点击「开始升级」

**方式二：上电按键触发**
1. 断电，按住 PB7（SOS键）上电
2. 连接串口
3. 选择固件文件，开始升级

升级完成后设备自动重启，新固件版本可在「设备配置」页查看。

### 状态监控

连接后自动每 3 秒刷新一次，显示：
- 运行阶段（待机/救援0~1h/救援1~3h/救援3~72h/测试模式）
- 电池电量（百分比，颜色区分高/中/低）
- GNSS 定位状态
- SOS 激活状态
- 累计发送次数
- 最新经纬度坐标

---

## 目录结构

```
host_tool/
├── main.py          入口
├── build.py         打包脚本（Windows/Linux 通用）
├── core/
│   ├── serial_port.py   串口管理
│   ├── device_api.py    JSON 命令接口
│   └── xmodem.py        XMODEM-CRC 协议
├── ui/
│   ├── main_window.py   主窗口
│   ├── tab_config.py    配置标签页
│   ├── tab_dfu.py       升级标签页
│   └── tab_monitor.py   监控标签页
└── dist/
    ├── ShenHaiYuTool        Linux 可执行文件
    └── ShenHaiYuTool.exe    Windows 可执行文件
```
