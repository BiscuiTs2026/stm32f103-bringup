cmake_minimum_required(VERSION 3.20)
project(pm_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Static CRT so the test binary runs without a Visual Studio environment.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(pm_host_tests
    test_main.c
    pm_osal_host.c
    pm_port_mock.c
    ../../src/core/pm_err.c
    ../../src/core/pm_event.c
    ../../src/core/pm_ringbuf.c
    ../../src/core/pm_gpio.c
    ../../src/core/pm_uart.c
    ../../src/core/pm_cmd.c
)

target_include_directories(pm_host_tests PRIVATE
    ../../include/pm
    ../../include
)

if(MSVC)
    target_compile_options(pm_host_tests PRIVATE /W4)
else()
    target_compile_options(pm_host_tests PRIVATE -Wall -Wextra -Wshadow -Werror)
endif()
