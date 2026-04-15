# MCU 引脚分配表

> MCU: N32L403KBQ7 (QFN32)

| 引脚号 | 引脚名 | 方向 | 网络名 | 功能说明 |
|--------|--------|------|--------|----------|
| 1 | VDD | PWR | VCC_3V3 | 电源 3.3V |
| 2 | PD14 | IN | — | OSC_IN (可选外部晶振) |
| 3 | PD15 | OUT | — | OSC_OUT |
| 4 | NRST | IN | MCU_NRST | 复位，外部按键/调试器 |
| 5 | VDDA | PWR | VCC_3V3 | 模拟电源 |
| 6 | PA0 | IN/OUT | UART2_RX | 短报文模块 UART2 RX |
| 7 | PA1 | OUT | LED1 | 运行指示 LED (低有效) |
| 8 | PA2 | OUT | UART2_TX | 短报文模块 UART2 TX |
| 9 | PA3 | IN | — | 预留 |
| 10 | PA4 | IN | AD_BAT | 电池电压 ADC 采集 |
| 11 | PA5 | IN | MCU_MAGKEY | 磁控开关输入 |
| 12 | PA6 | OUT | MCU_EN_PA | PA 功放使能 (高有效) |
| 13 | PA7 | OUT | CVPOW5V | 5V 功耗控制 |
| 14 | PB0 | IN | — | 预留 |
| 15 | PB1 | IN | — | 预留 |
| 16 | VSS | GND | GND | 地 |
| 17 | VDD | PWR | VCC_3V3 | 电源 3.3V |
| 18 | PA8 | IN | MCU_I_USB_IN | USB 插入检测 |
| 19 | PA9 | OUT | UART1_TX | GNSS 模块 RXD |
| 20 | PA10 | IN | UART1_RX | GNSS 模块 TXD |
| 21 | PA11 | IN/OUT | USB_DM | USB D- |
| 22 | PA12 | IN/OUT | USB_DP | USB D+ |
| 23 | PA13 | IN/OUT | SWDIO | SWD 调试数据 |
| 24 | PA14 | IN | SWCLK | SWD 调试时钟 |
| 25 | PA15 | IN | — | 预留 |
| 26 | PB3 | IN | — | 预留 |
| 27 | PB4 | OUT | LED2 | 电量指示 LED |
| 28 | PB5 | OUT | LED3 | 状态 LED |
| 29 | PB6 | OUT | MCU_EN_PGNSS | GNSS 模块电源使能 |
| 30 | PB7 | IN | KEY_FALL / KEY1 | 落水检测 / 按键 |
| 31 | PD0 | IN | MCU_BOOT | BOOT0 启动模式选择 |
| 32 | VSS | GND | GND | 地 |
| 33 | GND | GND | GND | 裸露焊盘 |

> PD14 (pin2) 同时连接 MCU_EN_PRDSS，用于短报文模块电源使能

## UART 分配

| 外设 | TX | RX | 波特率 | 对端模块 |
|------|----|----|--------|----------|
| UART1 | PA9 | PA10 | 9600 | B305-5Q GNSS |
| UART2 | PA2 | PA0 | 9600 | TD3203B 短报文 |
| UART_CH | MCU_CH_TXD | MCU_CH_RXD | 115200 | 调试串口 (H2) |

## ADC 分配

| 通道 | 引脚 | 网络 | 量程 | 说明 |
|------|------|------|------|------|
| ADC_CH4 | PA4 | AD_BAT | 0~3.3V | 电池电压，分压比 R23(1.2M)/R24(3.3M) |

电池电压计算：
```
V_BAT = V_ADC × (R23 + R24) / R24
      = V_ADC × (1.2M + 3.3M) / 3.3M
      = V_ADC × 1.364
```

## GPIO 功能说明

| 网络名 | 引脚 | 方向 | 有效电平 | 说明 |
|--------|------|------|----------|------|
| MCU_EN_PGNSS | PB6 | OUT | 高 | 控制 GNSS 模块 LDO 使能 |
| MCU_EN_PRDSS | PD14 | OUT | 高 | 控制短报文模块 LDO 使能 |
| MCU_EN_PA | PA6 | OUT | 高 | 控制 PA 功放 5V 使能 |
| CVPOW5V | PA7 | OUT | 高 | 控制 5V 总线功耗 |
| MCU_MAGKEY | PA5 | IN | 低 | 磁控开关触发 |
| KEY_FALL | PB7 | IN | 低 | 落水检测开关 |
| KEY1 | PB7 | IN | 低 | 用户按键 (复用) |
| MCU_I_USB_IN | PA8 | IN | 高 | USB 已插入 |
| MCU_BOOT | PD0 | IN | 高 | 进入 Bootloader |
| MCU_NRST | NRST | IN | 低 | 硬件复位 |
| LED1 | PA1 | OUT | 低 | 运行指示 |
| LED2 | PB4 | OUT | 低 | 电量指示 |
| LED3 | PB5 | OUT | 低 | 状态指示 |
