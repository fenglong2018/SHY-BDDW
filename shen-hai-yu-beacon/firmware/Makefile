# ============================================================
# 沈海渔示位标 固件 Makefile
# 目标: N32L403KBQ7 (Cortex-M4F)
# RTOS: RT-Thread Nano
# Windows 兼容版本
# ============================================================

TARGET  = beacon
BUILD   = build

CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

CPU     = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

# Windows 下目录创建命令（兼容正斜杠和反斜杠）
MKDIR   = mkdir
RM      = rmdir /s /q

# ---- RT-Thread Nano 源码路径 ----
RTT_DIR = RTThread/nano/rt-thread

RTT_SRCS = \
    $(RTT_DIR)/src/clock.c          \
    $(RTT_DIR)/src/components.c     \
    $(RTT_DIR)/src/device.c         \
    $(RTT_DIR)/src/idle.c           \
    $(RTT_DIR)/src/ipc.c            \
    $(RTT_DIR)/src/irq.c            \
    $(RTT_DIR)/src/kservice.c       \
    $(RTT_DIR)/src/mem.c            \
    $(RTT_DIR)/src/object.c         \
    $(RTT_DIR)/src/scheduler.c      \
    $(RTT_DIR)/src/thread.c         \
    $(RTT_DIR)/src/timer.c          \
    $(RTT_DIR)/libcpu/arm/cortex-m4/context_gcc.S \
    $(RTT_DIR)/libcpu/arm/cortex-m4/cpuport.c

RTT_INC = \
    -I$(RTT_DIR)/include            \
    -I$(RTT_DIR)/libcpu/arm/cortex-m4

# ---- 应用源文件 ----
C_SRCS = \
    Core/system.c               \
    Drivers/gpio.c              \
    Drivers/uart.c              \
    Drivers/adc.c               \
    Drivers/pwr.c               \
    Drivers/usb_cdc.c           \
    Drivers/flash_cfg.c         \
    Drivers/boot_confirm.c      \
    RTThread/board.c            \
    App/thread_gnss.c           \
    App/thread_rdss.c           \
    App/thread_beacon.c         \
    App/thread_led.c            \
    App/thread_adc.c            \
    App/thread_config.c         \
    App/main.c

AS_SRCS = Core/startup_n32l403.s

# ---- 头文件路径 ----
INCLUDES = -ICore -IDrivers -IRTThread -IApp $(RTT_INC)

# ---- 编译标志 ----
CFLAGS  = $(CPU) $(INCLUDES) \
          -std=c11 -Wall -Wextra \
          -ffunction-sections -fdata-sections \
          -DUSE_RTTHREAD -DN32L403 \
          -O2 -g

ASFLAGS = $(CPU) -g

# ---- 链接标志 ----
LDFLAGS = $(CPU) \
          -Tn32l403.ld \
          -Wl,--gc-sections \
          -Wl,-Map=$(BUILD)/$(TARGET).map \
          -lc -lm -lnosys \
          --specs=nano.specs

# ---- 目标文件 ----
RTT_C_SRCS  = $(filter %.c,  $(RTT_SRCS))
RTT_AS_SRCS = $(filter %.S,  $(RTT_SRCS))

OBJS = \
    $(addprefix $(BUILD)/, $(C_SRCS:.c=.o))      \
    $(addprefix $(BUILD)/, $(AS_SRCS:.s=.o))     \
    $(addprefix $(BUILD)/, $(RTT_C_SRCS:.c=.o))  \
    $(addprefix $(BUILD)/, $(RTT_AS_SRCS:.S=.o))

# ---- 辅助宏：Windows 下创建目录（路径含正斜杠，转换为反斜杠） ----
# 使用 cmd /c if not exist ... mkdir ...
define MAKEDIR
cmd /c if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
endef

# ---- 规则 ----
.PHONY: all clean flash size gendbg

all: gendbg $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin
	$(SIZE) $(BUILD)/$(TARGET).elf

gendbg:
	python gen_debug.py

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# C 源文件编译规则
$(BUILD)/%.o: %.c
	$(call MAKEDIR,$(dir $(BUILD)/$*.o))
	$(CC) $(CFLAGS) -c $< -o $@

# .s 汇编（小写）
$(BUILD)/%.o: %.s
	$(call MAKEDIR,$(dir $(BUILD)/$*.o))
	$(AS) $(ASFLAGS) -c $< -o $@

# .S 汇编（大写，RT-Thread context_gcc.S）
$(BUILD)/%.o: %.S
	$(call MAKEDIR,$(dir $(BUILD)/$*.o))
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary -S $< $@

flash: $(BUILD)/$(TARGET).hex
	openocd -f interface/stlink.cfg \
	        -f target/stm32f1x.cfg \
	        -c "program $(BUILD)/$(TARGET).hex verify reset exit"

size:
	$(SIZE) $(BUILD)/$(TARGET).elf

clean:
	cmd /c if exist "$(subst /,\,$(BUILD))" $(RM) "$(subst /,\,$(BUILD))"
