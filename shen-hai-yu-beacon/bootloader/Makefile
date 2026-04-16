# ============================================================
# Bootloader Makefile
# Flash: 0x08000000, 16KB
# ============================================================

TARGET  = bootloader
BUILD   = build

CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

CPU     = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(UNAME_S),Linux)
  MKDIR_P = mkdir -p
  RM_RF   = rm -rf $(BUILD)
else ifeq ($(UNAME_S),Darwin)
  MKDIR_P = mkdir -p
  RM_RF   = rm -rf $(BUILD)
else
  MKDIR_P = cmd /c if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
  RM_RF   = cmd /c if exist "$(subst /,\,$(BUILD))" rmdir /s /q "$(subst /,\,$(BUILD))"
endif

C_SRCS = \
    boot_main.c     \
    boot_hw.c       \
    boot_flash.c    \
    boot_flag.c     \
    boot_usb.c      \
    xmodem.c

AS_SRCS = ../firmware/Core/startup_n32l403.s

INCLUDES = -I. -I../firmware/Core -I../firmware/Drivers

CFLAGS  = $(CPU) $(INCLUDES) \
          -std=c11 -Wall \
          -ffunction-sections -fdata-sections \
          -DN32L403 -DBOOTLOADER \
          -O2 -g

ASFLAGS = $(CPU) -g

LDFLAGS = $(CPU) \
          -Tn32l403_boot.ld \
          -Wl,--gc-sections \
          -Wl,-Map=$(BUILD)/$(TARGET).map \
          -lc -lm -lnosys \
          --specs=nano.specs

OBJS = \
    $(addprefix $(BUILD)/, $(C_SRCS:.c=.o)) \
    $(addprefix $(BUILD)/, $(notdir $(AS_SRCS:.s=.o)))

.PHONY: all clean size

all: $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin
	$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/%.o: %.c
	@$(MKDIR_P) $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/startup_n32l403.o: ../firmware/Core/startup_n32l403.s
	@$(MKDIR_P) $(BUILD)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary -S $< $@

clean:
	$(RM_RF)
