# NeuralAutoTune, non-secure application image
#
#   make         build/NeuralAutoTune_uTK.elf, .list and .map
#   make clean
#
# Needs arm-none-eabi-gcc (the one in STM32CubeIDE works; board.sh puts it
# on PATH).
#
# Copyright 2026 Sher Amir Singh Dullat
# SPDX-License-Identifier: Apache-2.0

TARGET   := NeuralAutoTune_uTK
BUILD    := build
PREFIX   ?= arm-none-eabi-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
OBJDUMP  := $(PREFIX)objdump
SIZE     := $(PREFIX)size

DRV      := ../Drivers
MTK      := mtk3_bsp2
NPU      := Middlewares/ST/AI/Npu

# board bring-up
CUBE_SRC := $(wildcard Core/Src/*.c)
ASM_SRC  := Core/Startup/startup_stm32n657x0hxq.s

NATUNE_SRC := $(wildcard natune/src/*/*.c) $(wildcard natune/port/stm32n6570dk/*.c)

APP_SRC  := $(wildcard app/*.c)

# generated network code
NET_SRC  := $(wildcard X-CUBE-AI/network/*.c)

ATON_SRC := $(NPU)/Devices/STM32N6xx/mcu_cache.c $(wildcard $(NPU)/ll_aton/*.c)

HAL_MODS := hal hal_cortex hal_dma hal_dma_ex hal_exti hal_gpio hal_i2c hal_i2c_ex \
            hal_mdf hal_pwr hal_pwr_ex hal_rcc hal_rcc_ex hal_sai hal_sai_ex \
            hal_tim hal_tim_ex hal_uart hal_uart_ex ll_exti ll_rcc ll_utils
HAL_SRC  := $(foreach m,$(HAL_MODS),$(DRV)/STM32N6xx_HAL_Driver/Src/stm32n6xx_$(m).c)

# micro T-Kernel 3.0 and BSP2
MTK_SRC  := $(wildcard $(MTK)/mtkernel/kernel/inittask/*.c) \
            $(wildcard $(MTK)/mtkernel/kernel/sysinit/*.c) \
            $(wildcard $(MTK)/mtkernel/kernel/tkernel/*.c) \
            $(wildcard $(MTK)/mtkernel/kernel/tstdlib/*.c) \
            $(wildcard $(MTK)/mtkernel/kernel/usermain/*.c) \
            $(wildcard $(MTK)/mtkernel/lib/libtk/*.c) \
            $(wildcard $(MTK)/mtkernel/lib/libtm/*.c) \
            $(wildcard $(MTK)/sysdepend/stm32_cube/*.c) \
            $(wildcard $(MTK)/sysdepend/stm32_cube/cpu/core/armv8m/*.c) \
            $(wildcard $(MTK)/sysdepend/stm32_cube/lib/libtk/*.c) \
            $(wildcard $(MTK)/sysdepend/stm32_cube/lib/libtk/cpu/core/armv8m/*.c) \
            $(wildcard $(MTK)/sysdepend/stm32_cube/lib/libtk/cpu/stm32n6/*.c) \
            $(wildcard $(MTK)/sysdepend/stm32_cube/lib/libtm/discovery_stm32n657/*.c)
MTK_ASM  := $(MTK)/sysdepend/stm32_cube/cpu/core/armv8m/dispatch.S

C_SRC    := $(CUBE_SRC) $(NATUNE_SRC) $(APP_SRC) $(NET_SRC) $(ATON_SRC) $(HAL_SRC) $(MTK_SRC)

CPU      := -mcpu=cortex-m55 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb

DEFS     := -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx \
            -DLL_ATON_OSAL=1 -DLL_ATON_PLATFORM=9 -DLL_ATON_RT_MODE=2 \
            -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=0 \
            -D_STM32CUBE_DISCOVERY_N657_

INCS     := -Inatune/include -Inatune/src/core -Inatune/src/dsp -Inatune/src/pitch \
            -Inatune/src/npu -Inatune/src/diag -Inatune/port/stm32n6570dk -Iapp \
            -ICore/Inc \
            -I$(DRV)/STM32N6xx_HAL_Driver/Inc -I$(DRV)/STM32N6xx_HAL_Driver/Inc/Legacy \
            -I$(DRV)/CMSIS/Device/ST/STM32N6xx/Include -I$(DRV)/CMSIS/Include \
            -I$(DRV)/CMSIS/DSP/Include -I$(DRV)/CMSIS/DSP/PrivateInclude \
            -I$(NPU)/ll_aton -I$(NPU)/Devices/STM32N6xx -IX-CUBE-AI/network \
            -I$(MTK) -I$(MTK)/config -I$(MTK)/include -I$(MTK)/mtkernel/kernel/knlinc

CFLAGS   := $(CPU) -std=gnu11 -g3 -O2 $(DEFS) $(INCS) -ffunction-sections -fdata-sections \
            -Wall -flax-vector-conversions --specs=nano.specs -MMD -MP
ASFLAGS  := $(CPU) -g3 -DDEBUG -D_STM32CUBE_DISCOVERY_N657_ $(INCS) -x assembler-with-cpp \
            --specs=nano.specs -MMD -MP
LDFLAGS  := $(CPU) -T STM32N657X0HXQ_LRUN.ld --specs=nosys.specs --specs=nano.specs \
            -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--gc-sections -static \
            -Wl,--print-memory-usage -Wl,--start-group -lc -lm -Wl,--end-group

OBJS := $(addprefix $(BUILD)/,$(patsubst %.c,%.o,$(subst ../,up/,$(C_SRC)))) \
        $(addprefix $(BUILD)/,$(patsubst %.s,%.o,$(ASM_SRC))) \
        $(addprefix $(BUILD)/,$(patsubst %.S,%.o,$(MTK_ASM)))

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).list
	@$(SIZE) $<

$(BUILD)/$(TARGET).elf: $(OBJS) STM32N657X0HXQ_LRUN.ld
	@echo "LD   $@"
	@$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD)/$(TARGET).list: $(BUILD)/$(TARGET).elf
	@$(OBJDUMP) -h -S $< > $@

$(BUILD)/up/%.o: ../%.c
	@mkdir -p $(dir $@)
	@echo "CC   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "AS   $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "AS   $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
-include $(OBJS:.o=.d)
