# CMake toolchain file for arm-none-eabi-gcc (STM32F1).
# Override the toolchain root with -DTOOLCHAIN_ROOT=<dir>/bin if needed.

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED TOOLCHAIN_ROOT)
  set(TOOLCHAIN_ROOT "C:/Users/CHforeve/AppData/Local/stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin")
endif()

set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/arm-none-eabi-gcc.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_ROOT}/arm-none-eabi-gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/arm-none-eabi-g++.exe")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_ROOT}/arm-none-eabi-objcopy.exe")
set(CMAKE_SIZE         "${TOOLCHAIN_ROOT}/arm-none-eabi-size.exe")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
