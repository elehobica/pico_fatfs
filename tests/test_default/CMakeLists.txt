cmake_minimum_required(VERSION 3.13)

# define "w" board to use cyw43_arch_xxx() function
if(PICO_BOARD)
    unset(PICO_BOARD)
endif()
if(PICO_PLATFORM STREQUAL rp2350)
    set(PICO_BOARD pico2_w)
else()
    set(PICO_BOARD pico_w)
endif()


include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

set(PROJECT_NAME "pico_fatfs_test_default" C CXX)
project(${PROJECT_NAME})
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(../.. pico_fatfs)

add_executable(${PROJECT_NAME}
    ../common/benchmark.cpp
    main.cpp
)

#pico_enable_stdio_usb(${PROJECT_NAME} 0)
#pico_enable_stdio_uart(${PROJECT_NAME} 1)

target_link_libraries(${PROJECT_NAME}
    hardware_adc
    hardware_uart
    pico_stdlib
    pico_cyw43_arch_none
    pico_fatfs
)
pico_add_extra_outputs(${PROJECT_NAME})
