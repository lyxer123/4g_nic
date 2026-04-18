# 编译指南

## 项目结构

```
spiNetwork/esp32_wroom_host/
├── CMakeLists.txt          # 根项目配置
├── sdkconfig               # ESP-IDF 配置
├── main/                   # main 组件目录
│   ├── CMakeLists.txt      # 组件配置（选择版本）
│   ├── main.c             # 改进版（轮询驱动）
│   └── main_interrupt_driven.c  # 中断驱动版
└── build/                  # 编译输出目录
```

## 选择版本

编辑 `main/CMakeLists.txt`：

```cmake
# 编译中断驱动版本（推荐生产使用）
idf_component_register(
    SRCS "main_interrupt_driven.c"
    INCLUDE_DIRS "."
)

# 或者编译改进版（推荐开发使用）
# idf_component_register(
#     SRCS "main.c"
#     INCLUDE_DIRS "."
# )
```

## 编译步骤

### 1. 清理之前的构建（重要！）

```bash
cd F:\github\4g_nic\spiNetwork\esp32_wroom_host
idf.py fullclean
```

### 2. 设置目标芯片

```bash
idf.py set-target esp32
```

### 3. 编译

```bash
idf.py build
```

### 4. 烧录和监视

```bash
idf.py -p COM3 flash
idf.py monitor
```

## 切换版本

如果要切换版本：

1. 修改 `main/CMakeLists.txt` 中的 SRCS
2. 清理构建: `idf.py fullclean`
3. 重新编译: `idf.py build`

## 故障排除

### 错误：undefined reference to `app_main`

**原因**：项目结构不正确或选择了错误的源文件

**解决**：
- 确保源文件在 `main/` 目录中
- 确保 `main/CMakeLists.txt` 正确设置了 SRCS
- 运行 `idf.py fullclean` 后重新编译

### 错误：找不到头文件

**解决**：
- 确保已运行 `idf.py set-target esp32`
- 检查 ESP-IDF 环境变量是否正确设置

### 编译警告

一些格式相关的警告（如 `%lu` vs `%u`）不影响功能，可以忽略或后续修复。
