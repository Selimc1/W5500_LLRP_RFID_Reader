# Minimal GNU Make build (arm-none-eabi-gcc). STM32CubeIDE is not required.
TARGET  = W5500_LLRP_RFID_Reader
FAMILY  = STM32F4xx
DEFS    = -DSTM32F407xx
CPU     = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
LDSCRIPT = STM32F407VGTX_FLASH.ld

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

SRCS = $(filter-out ,$(wildcard Core/Src/*.c)) $(wildcard Core/Startup/*.s)
INCS = -ICore/Inc -IDrivers/CMSIS/Include -IDrivers/CMSIS/Device/ST/$(FAMILY)/Include

CFLAGS  = $(CPU) $(DEFS) $(INCS) -O2 -Wall -ffunction-sections -fdata-sections
LDFLAGS = $(CPU) -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,-Map=build/$(TARGET).map

all: build/$(TARGET).bin

build/$(TARGET).elf: $(SRCS) | build
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@
	$(SIZE) $@

build/$(TARGET).bin: build/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

build:
	mkdir -p build

flash: build/$(TARGET).bin
	st-flash --reset write $< 0x08000000

clean:
	rm -rf build

.PHONY: all flash clean
