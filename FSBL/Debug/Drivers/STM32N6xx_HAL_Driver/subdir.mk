################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_adc.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_adc_ex.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cortex.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma_ex.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_exti.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_gpio.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_hcd.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_mmc.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_mmc_ex.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr_ex.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc_ex.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rif.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sd.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sd_ex.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sdio.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart_ex.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_xspi.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_dma.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_exti.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_gpio.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_rcc.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_sdmmc.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_ucpd.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_usb.c \
../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_utils.c 

OBJS += \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc_ex.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cortex.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma_ex.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_exti.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_gpio.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_hcd.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc_ex.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr_ex.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc_ex.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rif.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd_ex.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sdio.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart_ex.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_xspi.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_dma.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_exti.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_gpio.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_rcc.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_sdmmc.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_ucpd.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_usb.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_utils.o 

C_DEPS += \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc_ex.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cortex.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma_ex.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_exti.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_gpio.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_hcd.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc_ex.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr_ex.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc_ex.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rif.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd_ex.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sdio.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart_ex.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_xspi.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_dma.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_exti.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_gpio.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_rcc.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_sdmmc.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_ucpd.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_usb.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_utils.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_adc.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc_ex.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_adc_ex.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cortex.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cortex.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma_ex.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma_ex.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_exti.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_exti.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_gpio.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_gpio.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_hcd.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_hcd.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_mmc.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc_ex.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_mmc_ex.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr_ex.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr_ex.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc_ex.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc_ex.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rif.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rif.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sd.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd_ex.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sd_ex.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sdio.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sdio.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart_ex.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart_ex.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_xspi.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_xspi.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_dma.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_dma.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_exti.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_exti.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_gpio.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_gpio.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_rcc.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_rcc.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_sdmmc.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_sdmmc.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_ucpd.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_ucpd.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_usb.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_usb.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_utils.o: ../../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_utils.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../Drivers/STM32N6xx_HAL_Driver/Src -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-STM32N6xx_HAL_Driver

clean-Drivers-2f-STM32N6xx_HAL_Driver:
	-$(RM) ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc_ex.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc_ex.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc_ex.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_adc_ex.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cortex.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cortex.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cortex.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cortex.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma_ex.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma_ex.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma_ex.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_dma_ex.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_exti.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_exti.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_exti.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_exti.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_gpio.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_gpio.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_gpio.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_gpio.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_hcd.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_hcd.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_hcd.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_hcd.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc_ex.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc_ex.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc_ex.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_mmc_ex.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr_ex.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr_ex.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr_ex.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_pwr_ex.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc_ex.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc_ex.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc_ex.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rcc_ex.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rif.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rif.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rif.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_rif.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd_ex.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd_ex.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd_ex.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sd_ex.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sdio.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sdio.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sdio.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_sdio.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart_ex.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart_ex.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart_ex.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_uart_ex.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_xspi.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_xspi.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_xspi.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_xspi.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_dma.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_dma.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_dma.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_dma.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_exti.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_exti.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_exti.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_exti.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_gpio.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_gpio.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_gpio.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_gpio.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_rcc.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_rcc.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_rcc.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_rcc.su
	-$(RM) ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_sdmmc.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_sdmmc.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_sdmmc.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_sdmmc.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_ucpd.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_ucpd.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_ucpd.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_ucpd.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_usb.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_usb.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_usb.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_usb.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_utils.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_utils.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_utils.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_ll_utils.su

.PHONY: clean-Drivers-2f-STM32N6xx_HAL_Driver

