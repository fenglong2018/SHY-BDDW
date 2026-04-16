# 沈海渔示位标 V1.0

> 海上救援北斗定位浮标，支持北斗短报文、GNSS定位、SOS报警、落水检测、USB配置。

---

## 目录

- [硬件平台](#硬件平台)
- [项目结构](#项目结构)
- [快速开始](#快速开始)
- [按键操作](#按键操作)
- [LED 指示](#led-指示)
- [救援模式工作时序](#救援模式工作时序)
- [USB 配置接口](#usb-配置接口)
- [固件升级（DFU）](#固件升级dfu)
- [调试指南](#调试指南)
- [接口引脚](#接口引脚)
- [文档](#文档)

---

## 硬件平台

| 模块 | 型号 | 说明 |
|------|------|------|
| MCU | N32L403KBQ7 | 主控，Cortex-M4，128KB Flash，32KB RAM |
| 定位模块 | B305-5Q | 北斗 B1/L1 双频 GNSS |
| 短报文模块 | TD3203B | 北斗 RDSS 短报文 |
| 北斗卡座 | XDSM-1023-4332A | Nano SIM |
| 充电芯片 | BQ24040DSQR | 100mA 锂电充电 |
| DCDC | TPS63802DLAR × 2 | 升降压，4.2V→5V / 4.2V→3.3V |
| USB | GT-USB-7103A + USB31-TYPE-C-FSABC | Type-C，充电+调试 |
| 电池 | 锂电池 450mAh | 3.7V |

### 电源树

```
Type-C (VBUS_5V)
  └─ BQ24040 (5V→4.2V) → 锂电池
       ├─ TPS63802 (4.2V→5V)  → VCC_5V  → 短报文模组 PA 供电
       └─ TPS63802 (4.2V→3V3) → VCC_3V3 → MCU / GNSS / LED / SOS
```

---

## 项目结构

```
shen-hai-yu-beacon/
├── bootloader/             Bootloader（16KB，0x08000000）
│   ├── boot_main.c         启动逻辑、App跳转
│   ├── boot_flash.c        Flash 擦写、版本更新
│   ├── xmodem.c            XMODEM-CRC 接收协议
│   ├── n32l403_boot.ld     链接脚本
│   └── Makefile
├── firmware/               应用固件（112KB，0x08004000）
│   ├── Core/               启动文件、系统时钟
│   ├── Drivers/            外设驱动（UART/ADC/GPIO/USB/Flash）
│   ├── App/                应用线程
│   │   ├── main.c          IPC 初始化、Flash 配置加载
│   │   ├── thread_beacon.c 业务主线程（状态机、按键、休眠）
│   │   ├── thread_gnss.c   GNSS SDBP 协议解析
│   │   ├── thread_rdss.c   RDSS TD3050 协议收发
│   │   ├── thread_led.c    LED 指示（PWM 呼吸流水）
│   │   ├── thread_adc.c    电池电量采集
│   │   └── thread_config.c USB JSON 配置接口
│   ├── RTThread/           RT-Thread Nano
│   ├── debug_config.json   调试开关配置
│   ├── gen_debug.py        生成 debug_config.h
│   ├── n32l403.ld          链接脚本（自动切换）
│   └── Makefile
├── docs/
│   ├── BOM.md              物料清单
│   ├── pinmap.md           引脚分配表
│   ├── interface.md        协议接口说明
│   └── architecture.md     架构与流程图
├── tools/
│   ├── beacon_tool.py      USB 串口监控工具
│   └── protocol.py         协议解析库
└── README.md
```

---

## 快速开始

### 1. 环境准备

```powershell
# 安装 ARM GCC（下载后安装）
# https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads

# 安装 make
choco install make

# 验证
arm-none-eabi-gcc --version
make --version
```

### 2. 获取 RT-Thread Nano

```powershell
cd firmware
git clone https://github.com/RT-Thread/rtthread-nano RTThread/nano
```

### 3. 编译

```powershell
# 编译 Bootloader
cd bootloader
make all

# 编译 App
cd ../firmware
make all
```

### 4. 烧录（ST-Link）

```powershell
# 烧录 Bootloader（首次，只需一次）
cd bootloader
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program build/bootloader.hex verify reset exit"

# 烧录 App
cd ../firmware
make flash
```

### 5. 连接调试

插入 Type-C USB，电脑识别为 COM 口，用串口工具打开（波特率任意），上电后显示：

```
=== ShenHaiYu-Beacon ===
FW: V1.0.0 | HW: 1.0.0 | SN: SHY-0000-000000 | UID: 0000000000000000
Commands: get_info/get_ver/get_sn/set_sn/set_hw_ver/get_uid/set_uid/get_bdid/get_status/enter_dfu
```

---

## 按键操作

SOS 按键（PB7，低有效）：

| 按住时长 | 动作 | LED 反馈 |
|----------|------|----------|
| < 50ms | 抖动，忽略 | — |
| 50ms ~ 500ms | **短按** → 电量显示 5s | 3颗按电量常亮 |
| 500ms ~ 1s | 忽略 | — |
| 1s ~ 3s | **长按** → 启动救援模式 | 1s时LED1亮作提示 |
| 3s ~ 8s | 忽略 | — |
| ≥ 8s | **超长按** → 测试模式 | 8s时3颗全亮确认 |

> 充电时（USB接入）按键同样有效，5s 后恢复充电显示。

---

## LED 指示

3 颗 LED 全部用于电量/状态显示：

### 充电中（USB接入）

| 电量 | 显示 |
|------|------|
| 0~33% | LED1 呼吸循环 |
| 33~66% | LED1→LED2 依次呼吸循环 |
| 66~95% | LED1→LED2→LED3 依次呼吸循环 |
| ≥ 95% | 3颗常亮（充满） |

> 呼吸效果：每颗 LED 渐亮 320ms + 保持 80ms，全灭间隔 160ms，软件 PWM 实现。

### 短按电量显示（5s）

| 电量 | 显示 |
|------|------|
| > 66% | LED1 + LED2 + LED3 常亮 |
| 33~66% | LED1 + LED2 常亮 |
| > 0% | LED1 常亮 |

### 测试模式

3 颗 LED 交替流水闪（200ms/颗）。

### 其他状态

全灭（省电）。

---

## 救援模式工作时序

长按 1~3s 激活，按以下时序工作：

| 时间段 | 发送间隔 | 工作窗口 | 休眠时长 |
|--------|----------|----------|----------|
| 0 ~ 1h | 2 分钟 | 10s（GNSS定位+短报文） | 110s |
| 1 ~ 3h | 5 分钟 | 10s | 290s |
| 3 ~ 72h | 15 分钟 | 10s | 890s |
| 72h 后 | — | 停止，回到深度休眠 | — |

**低电量保护**：电池电压 < 3.4V 时停止发报文，进入休眠。

**测试模式**（超长按 ≥ 8s）：每 2 分钟发送一次，任意按键退出。

---

## USB 配置接口

插入 Type-C，打开串口工具，发送 JSON 命令（每条以 `\n` 结尾）：

### 命令列表

| 命令 | 说明 |
|------|------|
| `{"cmd":"get_info"}` | 获取全部信息 |
| `{"cmd":"get_ver"}` | 读取固件版本（只读）和硬件版本 |
| `{"cmd":"set_hw_ver","hw_ver":"1.0.0"}` | 写入硬件版本到 Flash |
| `{"cmd":"get_sn"}` | 读取产品序列号 |
| `{"cmd":"set_sn","sn":"SHY-2026-000001"}` | 写入序列号到 Flash |
| `{"cmd":"get_uid"}` | 读取 16 位唯一识别码 |
| `{"cmd":"set_uid","uid":"A1B2C3D4E5F60001"}` | 写入唯一识别码（16位大写HEX） |
| `{"cmd":"get_bdid"}` | 读取北斗卡号 |
| `{"cmd":"get_status"}` | 读取运行状态 |
| `{"cmd":"enter_dfu"}` | 进入固件升级模式 |

### get_info 响应示例

```json
{
  "ok": true,
  "product": "ShenHaiYu-Beacon",
  "fw_ver": "V1.0.0",
  "hw_ver": "1.0.0",
  "sn": "SHY-2026-000001",
  "uid": "A1B2C3D4E5F60001",
  "bdid": "88888888",
  "bat_mv": 4050,
  "bat_pct": 83,
  "usb": true
}
```

### Flash 配置区布局

```
地址: 0x0801FC00（最后一页，1KB）

偏移   大小   内容
0x00   4B     magic = 0xBEAC0001
0x04   24B    sn[24]    产品序列号
0x1C   4B     hw_ver[4] 硬件版本 major.minor.patch
0x20   17B    uid[17]   16位唯一识别码
0x31   3B     rsvd[3]   fw_ver major.minor.patch（Bootloader写入）
0x34   4B     crc32     CRC32校验
```

---

## 固件升级（DFU）

### 方式一：USB 命令触发（推荐）

```
1. 串口工具发送：{"cmd":"enter_dfu"}
2. 设备自动重启进入 Bootloader
3. 用 XMODEM-CRC 发送 beacon.bin
4. 下载完成后自动更新版本号并重启
```

```powershell
# 使用上位机工具
python tools/dfu_tool.py --port COM3 --file firmware/build/beacon.bin
```

### 方式二：上电按键触发

上电时按住 PB7（SOS键），进入 Bootloader DFU 模式。

### Bootloader 交互

```
=== ShenHaiYu Beacon Bootloader ===
Firmware version: BL1.0.0
Waiting for firmware (XMODEM-CRC)...
Send .bin file via XMODEM-CRC, or press 'Q' to boot app.
Auto boot in 5s...
```

---

## 调试指南

### 调试模式切换

编辑 `firmware/debug_config.json`，`make all` 时自动生效：

```json
{
  "options": {
    "USE_BOOTLOADER": false,
    "LOG_LEVEL": 3
  }
}
```

| `USE_BOOTLOADER` | App Flash 起始 | 说明 |
|------------------|----------------|------|
| `false` | 0x08000000 | 调试模式，ST-Link 直接烧，无需 Bootloader |
| `true` | 0x08004000 | 生产模式，需先烧 Bootloader |

| `LOG_LEVEL` | 说明 |
|-------------|------|
| 0 | 关闭所有日志（出厂固件） |
| 1 | 仅 ERROR |
| 2 | ERROR + INFO（默认） |
| 3 | 全部（DEBUG） |

### 模块调试开关

```json
{
  "modules": {
    "GNSS":   true,
    "RDSS":   false,
    "BEACON": true,
    "LED":    false,
    "ADC":    true
  }
}
```

关闭的模块日志被编译器完全优化掉，零运行时开销。

### 典型调试场景

```powershell
# 场景1：功能开发（最快迭代）
# USE_BOOTLOADER=false, LOG_LEVEL=3, 所有模块=true
make clean && make all && make flash

# 场景2：单模块调试（只看 GNSS）
# GNSS=true, 其他=false, LOG_LEVEL=3
make all && make flash

# 场景3：出厂固件
# USE_BOOTLOADER=true, LOG_LEVEL=0
make clean && make all
```

### VS Code / Kiro 调试

按 `F5` 启动调试（需 ST-Link 连接），配置见 `.vscode/launch.json`。

断点推荐位置：
- `thread_beacon.c` → `_do_work_window()` 工作窗口入口
- `thread_gnss.c` → `_parse_lla3()` 定位数据解析
- `thread_rdss.c` → `_send_msg()` 短报文发送

---

## 接口引脚

| 引脚 | 网络名 | 方向 | 说明 |
|------|--------|------|------|
| PA1 | LED1 | OUT | 电量指示 LED1（低有效） |
| PB4 | LED2 | OUT | 电量指示 LED2（低有效） |
| PB5 | LED3 | OUT | 电量指示 LED3（低有效） |
| PA4 | AD_BAT | IN | 电池电压 ADC（分压 1.2M/3.3M） |
| PA5 | MCU_MAGKEY | IN | 磁控开关（低有效） |
| PB7 | KEY_FALL | IN | SOS 按键 / 落水检测（低有效） |
| PA8 | MCU_I_USB_IN | IN | USB 插入检测 |
| PA9 | UART1_TX | OUT | GNSS 模块 RXD（轮询TX） |
| PA10 | UART1_RX | IN | GNSS 模块 TXD（**DMA1 CH5**） |
| PA2 | UART2_TX | OUT | 短报文模块 RXD（轮询TX） |
| PA0 | UART2_RX | IN | 短报文模块 TXD（**DMA1 CH6**） |
| PA0 | UART2_RX | IN | 短报文模块 TXD |
| PA11 | USB_DM | I/O | USB D-（PMA硬件搬运，无需DMA） |
| PA12 | USB_DP | I/O | USB D+（PMA硬件搬运，无需DMA） |
| PA13 | SWDIO | I/O | SWD 调试数据 |
| PA14 | SWCLK | IN | SWD 调试时钟 |
| PB6 | MCU_EN_PGNSS | OUT | GNSS 模块电源使能（高有效） |
| PD14 | MCU_EN_PRDSS | OUT | 短报文模块电源使能（高有效） |
| PA6 | MCU_EN_PA | OUT | PA 功放使能（高有效） |
| PA7 | CVPOW5V | OUT | 5V 功耗控制 |

### SWD 调试口 H1（5pin）

```
Pin1 → VCC_3V3
Pin2 → SWDIO
Pin3 → SWCLK
Pin4 → NRST
Pin5 → GND
```

---

## 文档

| 文档 | 说明 |
|------|------|
| [docs/BOM.md](docs/BOM.md) | 完整物料清单 |
| [docs/pinmap.md](docs/pinmap.md) | MCU 引脚分配表 |
| [docs/interface.md](docs/interface.md) | GNSS/RDSS 协议接口说明 |
| [docs/architecture.md](docs/architecture.md) | 软件架构与流程图（Mermaid） |

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| V1.0.0 | 2026-04 | 初始版本，支持北斗短报文、GNSS、USB配置、Bootloader |
| V1.0.1 | 2026-04 | 代码质量优化：ADC中位数滤波、GNSS帧解析修复、RDSS位置格式修复、测试模式计时修复、DMA溢出监控 |

---

## 编译产物说明

编译完成后 `arm-none-eabi-size` 输出各段大小：

```
App 固件 (beacon.elf):
  text    data     bss     dec
  14604    124    2076   16804
  Flash 占用 ≈ 14.4KB / 48KB  (30%)
  RAM   占用 ≈  2.1KB / 32KB  (6.5%)

Bootloader (bootloader.elf):
  text    data     bss     dec
  8760     100     824    9684
  Flash 占用 ≈  8.7KB / 16KB  (54%)
  RAM   占用 ≈  0.9KB /  8KB  (11%)
```

| 段 | 含义 | 位置 |
|----|------|------|
| text | 代码 + 只读常量 | Flash |
| data | 有初始值的全局变量 | Flash存储，RAM运行 |
| bss | 无初始值的全局变量（清零） | 仅RAM |

> RT-Thread 线程栈和动态内存从堆分配，包含在 bss 段的堆空间内，实际 RAM 使用约 8~12KB，32KB 完全够用。
