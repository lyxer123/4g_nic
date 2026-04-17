cmake_minimum_required(VERSION 3.13)

# 初始化Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(rp2040_spi_host C CXX ASM)

# 初始化Pico SDK
pico_sdk_init()

# 添加可执行文件
add_executable(rp2040_spi_host
    main.c
)

# 链接Pico标准库
target_link_libraries(rp2040_spi_host
    pico_stdlib
    hardware_spi
    hardware_dma
    hardware_gpio
    hardware_timer
)

# 启用USB串口输出
pico_enable_stdio_usb(rp2040_spi_host 1)
pico_enable_stdio_uart(rp2040_spi_host 0)

# 生成uf2文件
pico_add_extra_outputs(rp2040_spi_host)
