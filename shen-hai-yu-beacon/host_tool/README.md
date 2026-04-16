# 沈海渔示位标 上位机工具

## 功能

| 标签页 | 功能 |
|--------|------|
| ⚙ 设备配置 | 读写 SN / UID / 硬件版本，查看固件版本、北斗卡号、电量 |
| ⬆ 固件升级 | XMODEM-CRC 协议升级固件，实时进度显示 |
| 📊 状态监控 | 实时显示运行阶段、电量、GNSS定位、SOS状态、累计发送次数 |

## 运行环境

- Python 3.10+
- Windows 10/11

## 安装依赖

```powershell
pip install PyQt6 pyserial pyinstaller
```

## 直接运行

```powershell
cd host_tool
python main.py
```

## 打包成 EXE

```powershell
cd host_tool
python build.py
# 生成 dist/ShenHaiYuTool.exe
```

## 使用说明

### 连接设备

1. 用 Type-C USB 连接设备
2. 在「端口」下拉框选择对应 COM 口
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

## 目录结构

```
host_tool/
├── main.py          入口
├── build.py         打包脚本
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
    └── ShenHaiYuTool.exe  打包输出
```
