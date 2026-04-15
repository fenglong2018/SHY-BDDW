# BOM 物料清单

> 沈海渔示位标 V1.0 | 更新日期：2026-04-13

## 主要芯片

| 位号 | 名称 | 型号 | 数量 | 备注 |
|------|------|------|------|------|
| U7 | MCU | N32L403KBQ7 | 1 | 主控，QFN32 |
| U10 | GNSS定位模块 | B305-5Q | 1 | 北斗B1/L1双频 |
| U13 | 短报文模块 | TD3203B | 1 | 北斗RDSS |
| U3 | DCDC (ARM供电) | TPS63802DLAR | 1 | 4.2V→3.3V |
| U4 | DCDC (RDSS供电) | TPS63802DLAR | 1 | 4.2V→5V |
| U6 | 充电芯片 | BQ24040DSQR | 1 | 100mA锂电充电 |
| U11 | USB ESD保护 | GT-USB-7103A | 1 | USB2.0 ESD |
| U5 | Type-C连接器 | USB31-TYPE-C-FSABC | 1 | 充电+调试 |
| U12 | SIM ESD保护 | LM-RCLAMP0524P-N | 1 | SIM卡ESD |
| U18 | SWD ESD保护 | LM-RCLAMP0524P-N | 1 | 调试口ESD |
| SIM1 | 北斗卡座 | XDSM-1023-4332A | 1 | Nano SIM |
| U9 | 二维码标识 | 嘉立创5x5 | 1 | 省海鱼V0.1 |

## 电源部分

| 位号 | 型号 | 数量 | 备注 |
|------|------|------|------|
| L1, L2 | 470nH 电感 | 2 | DCDC储能 |
| C1 | 10uF | 1 | ARM DCDC输入 |
| C2, C3 | 22uF / 100nF | 2 | ARM DCDC输出 |
| C4, C12 | 22uF | 2 | RDSS DCDC输入 |
| C6, C10 | 100nF / 22uF | 2 | RDSS DCDC输出 |
| C7, C17 | 1uF | 2 | DCDC补偿 |
| C8 | 1uF | 1 | 充电芯片 |
| R3 | 511k | 1 | ARM DCDC FB上 |
| R4 | 91k | 1 | ARM DCDC FB下 |
| R7 | 806k | 1 | RDSS DCDC FB上 |
| R8 | 91k | 1 | RDSS DCDC FB下 |
| D10 | D10SMAJ24CA | 1 | TVS保护 |

## USB 部分

| 位号 | 型号 | 数量 | 备注 |
|------|------|------|------|
| R11, R12 | 5.1k | 2 | CC1/CC2下拉 |
| R13 | 825Ω | 1 | 充电限流 |
| R14 | 2k | 1 | 充电设置 |
| R16 | — | 1 | 充电指示 |
| C17 | 1uF | 1 | VBUS滤波 |
| D1, D2 | TPESD0402G12V | 2 | USB ESD |

## IO控制部分

| 位号 | 型号 | 数量 | 备注 |
|------|------|------|------|
| R15, R17, R18, R19 | 10k | 4 | LED限流 |
| R20 | 10k | 1 | KEY上拉 |
| R23 | 1.2M | 1 | 电池电压分压上 |
| R24 | 3.3M | 1 | 电池电压分压下 |
| R21, R25, R27, R29 | 1M | 4 | PMOS栅极 |
| R22, R26, R30 | 10k | 3 | 使能下拉 |
| Q1 | PMOS | 1 | MCU_EN_PGNSS |
| Q2 | PMOS | 1 | MCU_EN_PRDSS |
| Q3, Q4 | PMOS | 2 | MCU_EN_PA |
| Q25 | RZM001P02T2L | 1 | 磁控开关 |
| SW2 | B3U-1000P | 1 | 按键 |
| D27 | TPESD0402G05V | 1 | 磁控ESD |
| C61 | 100nF | 1 | 磁控滤波 |
| R107 | 200k | 1 | 磁控上拉 |
| R159 | 10k | 1 | 磁控下拉 |
| R202 | 499k | 1 | USB检测分压 |
| R203 | 100k | 1 | USB检测 |
| R204 | 10k | 1 | USB检测下拉 |
| C105 | 100nF | 1 | USB检测滤波 |

## 北斗模块部分

| 位号 | 型号 | 数量 | 备注 |
|------|------|------|------|
| L4 | 1.8nH | 1 | GNSS RF匹配 |
| L5~L8 | 1.8nH | 4 | RDSS RF匹配 |
| C38 | 1uF | 1 | GNSS VCC滤波 |
| C39, C40 | 100pF | 2 | GNSS RF滤波 |
| C41~C43 | 33pF | 3 | RDSS RF滤波 |
| C44 | 1uF | 1 | SIM VCC滤波 |
| C45 | 33pF | 1 | SIM滤波 |
| C46, C47 | 100pF | 2 | RDSS RF滤波 |
| C48~C51 | 47uF | 4 | RDSS 5V滤波 |
| C52, C54 | 100nF | 2 | RDSS 3V3滤波 |
| C53 | 1uF | 1 | RDSS 3V3滤波 |
| C35, C36 | 100nF | 2 | SIM滤波 |
| R139~R142 | 22Ω | 4 | SIM串联保护 |
| R143~R145 | 10k | 3 | SIM上拉 |
| R146 | 2M | 1 | 插卡检测 |
| D14~D16 | TPESD0402G05V | 3 | SIM ESD |

## MCU 部分

| 位号 | 型号 | 数量 | 备注 |
|------|------|------|------|
| C13~C16 | 100nF | 4 | VDD去耦 |
| R28, R31 | 10k | 2 | BOOT/NRST上拉 |
| R34~R39 | 22Ω | 6 | 串联保护 |
| D18, D19 | TPESD0402G05V | 2 | 串口ESD |
| H1 | 5pin | 1 | SWD调试口 |
| H2 | 3pin | 1 | 串口打印口 |

## 散装电容（电源滤波）

| 位号 | 容值 | 数量 |
|------|------|------|
| C5, C9 | 22uF | 2 | VCC_5V滤波 |
| C18, C19 | 220uF | 2 | BAT滤波 |
| C20, C21 | 22uF | 2 | VCC_3V3滤波 |
| C11, C111 | 100uF | 2 | 5V_PA滤波 |
