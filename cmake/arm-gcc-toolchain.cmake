# CMake toolchain file for arm-none-eabi-gcc (STM32F1).
# The directory containing arm-none-eabi-gcc must be provided in one of two ways:
#   1) -DTOOLCHAIN_ROOT=<dir>/bin on the cmake command line
#   2) environment variable ARM_GCC_ROOT=<dir>/bin
# There is no hard-coded default; if neither is set, configure fails with a hint.

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED TOOLCHAIN_ROOT)
  if(NOT "$ENV{ARM_GCC_ROOT}" STREQUAL "")
    set(TOOLCHAIN_ROOT "$ENV{ARM_GCC_ROOT}")
  else()
    message(FATAL_ERROR
      "TOOLCHAIN_ROOT is not set.\n"
      "Pass -DTOOLCHAIN_ROOT=<dir>/bin (the directory containing arm-none-eabi-gcc.exe), "
      "or set the ARM_GCC_ROOT environment variable to that directory.")
  endif()
endif()

# Accept Windows-style backslash paths from -D or the environment.
file(TO_CMAKE_PATH "${TOOLCHAIN_ROOT}" TOOLCHAIN_ROOT)
set(TOOLCHAIN_ROOT "${TOOLCHAIN_ROOT}" CACHE PATH "Directory containing arm-none-eabi-gcc" FORCE)
# Forward the value into try_compile sub-projects (compiler ABI detection).
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TOOLCHAIN_ROOT)

set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/arm-none-eabi-gcc.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_ROOT}/arm-none-eabi-gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/arm-none-eabi-g++.exe")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_ROOT}/arm-none-eabi-objcopy.exe")
set(CMAKE_SIZE         "${TOOLCHAIN_ROOT}/arm-none-eabi-size.exe")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
