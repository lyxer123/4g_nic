# Main component CMakeLists.txt
# 选择要编译的版本：
# 1. main_interrupt_driven.c - 中断驱动版本（推荐生产使用）
# 2. main.c - 改进版（推荐开发使用）

idf_component_register(
    SRCS "main_interrupt_driven.c"
    # SRCS "main.c"
    INCLUDE_DIRS "."
)
