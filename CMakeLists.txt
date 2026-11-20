cmake_minimum_required(VERSION 3.20)
project(peripheral_model C ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(PM_ROOT     ${CMAKE_CURRENT_SOURCE_DIR})
set(PM_PLATFORM ${PM_ROOT}/platform/stm32f103)

set(PM_CPU_FLAGS  -mcpu=cortex-m3 -mthumb -mfloat-abi=soft)
set(PM_WARN_FLAGS -Wall -Wextra -Wshadow -Wundef
                  -Werror=implicit-function-declaration)
set(PM_OPT_FLAGS  -Os -g3 -ffunction-sections -fdata-sections)

# ------------------------------------------------------------------------ HAL
add_library(pm_hal STATIC
    third_party/stm32f1xx_hal/src/stm32f1xx_hal.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_cortex.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_adc.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_adc_ex.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_dma.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_flash.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_flash_ex.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_gpio.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_gpio_ex.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_pwr.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_rcc.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_rcc_ex.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_spi.c
    third_party/stm32f1xx_hal/src/stm32f1xx_hal_uart.c
)
target_include_directories(pm_hal
    PUBLIC  ${PM_ROOT}/third_party/stm32f1xx_hal/inc
            ${PM_ROOT}/third_party/cmsis/device/include
            ${PM_ROOT}/third_party/cmsis/include
    PRIVATE ${PM_PLATFORM}
)
target_compile_definitions(pm_hal PUBLIC
    USE_HAL_DRIVER
    STM32F103xB
)
# Vendor code is compiled without our warning policy on purpose.
target_compile_options(pm_hal PRIVATE
    ${PM_CPU_FLAGS} -Os -g -ffunction-sections -fdata-sections -w)

# ---------------------------------------------------------------- core model
add_library(pm_core STATIC
    src/core/pm_err.c
    src/core/pm_event.c
    src/core/pm_ringbuf.c
    src/core/pm_gpio.c
    src/core/pm_uart.c
    src/core/pm_cmd.c
    src/core/pm_cmd_table.c
)
target_include_directories(pm_core PUBLIC ${PM_ROOT}/include/pm ${PM_ROOT}/include)
target_compile_options(pm_core PRIVATE
    ${PM_CPU_FLAGS} ${PM_WARN_FLAGS} ${PM_OPT_FLAGS})

# ---------------------------------------------------------------------- OSAL
add_library(pm_osal STATIC
    src/osal/pm_osal_port_cm3.c
)
target_include_directories(pm_osal
    PUBLIC  ${PM_ROOT}/include/pm ${PM_ROOT}/include
    PRIVATE ${PM_ROOT}/third_party/cmsis/device/include
            ${PM_ROOT}/third_party/cmsis/include
)
target_compile_definitions(pm_osal PRIVATE
    STM32F103xB
)
target_compile_options(pm_osal PRIVATE
    ${PM_CPU_FLAGS} ${PM_WARN_FLAGS} ${PM_OPT_FLAGS})

# -------------------------------------------------------- STM32F1 port + board
# Encoder module switch (decision 0018 §6): with the module off the port source
# is not compiled at all, so neither pm_encoder_init() nor EXTI9_5_IRQHandler
# exists and nothing arms EXTI8/9 or the NVIC line. encoder_dev turns into a
# stub that stays "not ready", which keeps the ENC reply on the existing
# err=init wording instead of growing a second disabled state.
option(PM_ENABLE_ENCODER "Build the quadrature encoder (PA8/PA9, EXTI9_5)" ON)
if(PM_ENABLE_ENCODER)
    set(PM_ENCODER_DEFINE PM_ENABLE_ENCODER=1)
else()
    set(PM_ENCODER_DEFINE PM_ENABLE_ENCODER=0)
endif()

add_library(pm_port_stm32f1 STATIC
    src/port/stm32f1/pm_port_stm32f1.c
    src/port/stm32f1/pm_pwm_stm32f1.c
    src/port/stm32f1/pm_track_stm32f1.c
    src/port/stm32f1/pm_relay_stm32f1.c
    src/port/stm32f1/pm_motor_stm32f1.c
    src/port/stm32f1/pm_timer_stm32f1.c
    src/port/stm32f1/pm_i2c1_stm32f1.c
    src/port/stm32f1/pm_adc1_stm32f1.c
    src/port/stm32f1/pm_sonar_stm32f1.c
    src/port/stm32f1/pm_keypad_stm32f1.c
    ${PM_PLATFORM}/board_f103c8.c
    ${PM_PLATFORM}/pm_platform.c
)
if(PM_ENABLE_ENCODER)
    target_sources(pm_port_stm32f1 PRIVATE
        src/port/stm32f1/pm_encoder_stm32f1.c)
endif()
target_compile_definitions(pm_port_stm32f1 PRIVATE ${PM_ENCODER_DEFINE})
target_include_directories(pm_port_stm32f1
    PUBLIC  ${PM_ROOT}/include/pm ${PM_ROOT}/include
            ${PM_PLATFORM}
    PRIVATE ${PM_ROOT}/src/port/stm32f1
)
target_compile_options(pm_port_stm32f1 PRIVATE
    ${PM_CPU_FLAGS} ${PM_WARN_FLAGS} ${PM_OPT_FLAGS})
target_link_libraries(pm_port_stm32f1 PUBLIC pm_core pm_osal pm_hal)

# ------------------------------------------------------------------- firmware
# Component layer: device logic that has no register access of its own. Bus
# access is injected through a callback so the entry glue owns the I2C polling.
add_library(pm_components STATIC
    src/components/oled_ch1116/oled_ch1116.c
    src/components/oled_ui/oled_ui_page.c
    src/components/oled_ui/oled_ui_job.c
    src/components/aht20_dev/aht20_dev.c
    src/components/adc_dev/adc_dev.c
    src/components/keypad_dev/keypad_dev.c
    src/components/pm_cmd/pm_cmd_out.c
    src/components/pm_cmd/pm_cmd_console.c
    src/components/pwm_dev/pwm_dev.c
    src/components/track_dev/track_dev.c
    src/components/encoder_dev/encoder_dev.c
    src/components/relay_dev/relay_dev.c
    src/components/sonar_dev/sonar_dev.c
    src/components/motor_dev/motor_dev.c
    src/components/sys_cmds/sys_cmds.c
)
target_include_directories(pm_components
    PUBLIC  ${PM_ROOT}/src/components/oled_ch1116
            ${PM_ROOT}/src/components/oled_ui
            ${PM_ROOT}/src/components/aht20_dev
            ${PM_ROOT}/src/components/adc_dev
            ${PM_ROOT}/src/components/keypad_dev
            ${PM_ROOT}/src/components/pm_cmd
            ${PM_ROOT}/src/components/pwm_dev
            ${PM_ROOT}/src/components/track_dev
            ${PM_ROOT}/src/components/encoder_dev
            ${PM_ROOT}/src/components/relay_dev
            ${PM_ROOT}/src/components/sonar_dev
            ${PM_ROOT}/src/components/motor_dev
            ${PM_ROOT}/src/components/sys_cmds
            ${PM_ROOT}/include/pm
    PRIVATE ${PM_PLATFORM}
)
target_compile_options(pm_components PRIVATE
    ${PM_CPU_FLAGS} ${PM_WARN_FLAGS} ${PM_OPT_FLAGS})
target_compile_definitions(pm_components PRIVATE ${PM_ENCODER_DEFINE})

add_executable(pm_f103_demo
    src/app/main.c
    src/app/app_wiring.c
    platform/stm32f103/startup_stm32f103xb.s
    third_party/cmsis/device/source/system_stm32f1xx.c
)
target_include_directories(pm_f103_demo PRIVATE
    ${PM_ROOT}/include/pm ${PM_ROOT}/include
    ${PM_PLATFORM}
)
target_compile_options(pm_f103_demo PRIVATE
    ${PM_CPU_FLAGS} ${PM_WARN_FLAGS} ${PM_OPT_FLAGS})
target_compile_definitions(pm_f103_demo PRIVATE
    USE_HAL_DRIVER
    STM32F103xB
)

# SPI demo is compile-time optional: the board pins it would use (PB13/PB14/
# PB15) are shared with KEY2, the tracking sensor and the encoder button, so it
# stays off by default. Configure with -DPM_DEMO_SPI=ON to link the SPI path and
# measure its Flash/RAM cost.
option(PM_DEMO_SPI "Link the SPI2 loopback demo into pm_f103_demo" OFF)
if(PM_DEMO_SPI)
    target_compile_definitions(pm_f103_demo PRIVATE PM_DEMO_SPI=1)
endif()
# pm_core calls pm_port_get() and the port calls back into pm_core, so the
# static libraries are linked as a group instead of relying on a strict order.
target_link_libraries(pm_f103_demo PRIVATE
    -Wl,--start-group
    pm_port_stm32f1
    pm_components
    pm_core
    pm_osal
    pm_hal
    -Wl,--end-group
)

target_link_options(pm_f103_demo PRIVATE
    ${PM_CPU_FLAGS}
    -T${PM_PLATFORM}/stm32f103c8.ld
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/pm_f103_demo.map
    -Wl,--print-memory-usage
)

add_custom_command(TARGET pm_f103_demo POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:pm_f103_demo>
            $<TARGET_FILE_DIR:pm_f103_demo>/pm_f103_demo.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:pm_f103_demo>
            $<TARGET_FILE_DIR:pm_f103_demo>/pm_f103_demo.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:pm_f103_demo>
    COMMENT "Generating .bin/.hex and reporting size"
)
