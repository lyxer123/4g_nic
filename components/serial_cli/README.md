# Serial CLI 组件

## 功能概述

Serial CLI (Serial Command Line Interface) 组件提供基于 UART 的交互式命令行界面，支持本地命令执行和 PC 远程控制协议（PCAPI）。该组件实现了类似 Linux shell 的 REPL（Read-Eval-Print Loop）交互模式。

### 主要功能

- 交互式命令行界面（REPL）
- 支持 linenoise 命令行编辑
- PCAPI 协议支持（PC 远程控制）
- HTTP over UART 桥接
- 命令历史纪录
- 自动补全（Tab）
- 背格删除（Backspace）

## 核心特性

### 1. 交互式 CLI

```
4g_nic> help
Available commands:
  help      - Show this help
  modem_info - Show modem information
  reboot    - Reboot the device
  ...
4g_nic> 
```

### 2. PCAPI 协议

允许 PC 软件通过串口发送 HTTP 请求到设备的 Web Server：

```
PC → UART: PCAPI POST /api/wifi/ap 121\r\n
           {"wifi_enabled": true, "ssid": "...", ...}
           
ESP32 → UART: PCAPI_OUT 200 43\r\n
              {"status":"success"}
```

### 3. 命令历史

- 上下箭头浏览历史命令
- 支持多行历史记录
- 重启后清除

## 快速开始

### 初始化

```c
#include "serial_cli.h"

void app_main(void)
{
    // 其他初始化...
    
    // 启动串口 CLI
    serial_cli_start();
}
```

### API 接口

#### `serial_cli_start()`

启动 UART 交互式 CLI。

**函数原型：**
```c
void serial_cli_start(void);
```

**说明：**
- 创建 CLI 任务（独立 FreeRTOS 任务）
- 初始化 UART 控制台
- 启动 PCAPI 协议处理器
- 注册内置命令

**调用时机：**
- 在网络和 Web 服务初始化之后调用
- 确保 PCAPI 可以访问 HTTP 服务器

## PCAPI 协议详解

### 协议格式

#### 请求格式

**GET 请求：**
```
PCAPI GET /api/system/time\r\n
```

**POST 请求：**
```
PCAPI POST /api/wifi/ap 121\r\n
{"wifi_enabled": true, "ssid": "ESP_D7B119", ...}
```

**DELETE 请求：**
```
PCAPI DELETE /api/config/item\r\n
```

#### 响应格式

```
PCAPI_OUT <status_code> <body_length>\r\n
<body_content>
```

**示例：**
```
PCAPI_OUT 200 43\r\n
{"status":"success","message":"ok"}
```

### 支持的方法

| 方法 | 说明 | 格式 |
|------|------|------|
| GET | 查询数据 | `PCAPI GET /path` |
| POST | 提交数据 | `PCAPI POST /path <len>` |
| DELETE | 删除数据 | `PCAPI DELETE /path` |

### 数据传输流程

```
PC Software                    ESP32-S3
     │                             │
     │── PCAPI POST /api/... ─────►│
     │                             │ 解析 header
     │── JSON body ───────────────►│ 读取 body
     │                             │ 转发到 HTTP Server
     │                             │ 处理请求
     │◄── PCAPI_OUT 200 43 ───────│
     │◄── {"status":"success"} ───│
     │                             │
```

### 实现细节

#### Header 解析

```c
// 格式：PCAPI METHOD /path [body_length]
// 示例：PCAPI POST /api/wifi/ap 121
```

#### Body 读取

```c
// POST 请求需要读取 body
// 注意：需要先消耗 header 行残留的 \r\n
// 然后读取指定长度的 body 数据
```

#### 错误处理

| 错误码 | 说明 | 示例 |
|--------|------|------|
| 400 | 请求格式错误 | `{"status":"error","message":"invalid json"}` |
| 404 | 路径不存在 | `{"status":"error","message":"not found"}` |
| 408 | Body 读取超时 | `{"status":"error","message":"post body read timeout"}` |
| 500 | 服务器内部错误 | `{"status":"error","message":"oom"}` |

## 依赖关系

### 硬件依赖

- UART 端口（Console UART）
- USB-UART 转换器（用于 PC 连接）

### ESP-IDF 组件依赖

- `driver` (UART 驱动)
- `console` (命令行框架)
- `linenoise` (命令行编辑)
- `esp_http_server` (HTTP 服务器)
- `esp_netif` (网络接口)

### 项目组件依赖

- `webService` (HTTP 服务器，PCAPI 需要)
- `router_at` (AT 指令，可选共享 UART)

### CMake 配置

```cmake
idf_component_register(
    SRCS "serial_cli.c"
    INCLUDE_DIRS "include"
    REQUIRES driver console linenoise esp_http_server
    PRIV_REQUIRES webService router_at
)
```

## 配置选项

### menuconfig 配置

```
Component config --->
    ESP System Settings --->
        Console UART Configuration --->
            Console UART Port (UART0) --->
            Console UART Baud Rate (115200) --->
            
Serial CLI Configuration --->
    [*] Enable PCAPI protocol
    PCAPI Body Read Timeout (5000ms) --->
    CLI Line Buffer Size (256) --->
```

## 内置命令

### 系统命令

| 命令 | 说明 | 示例 |
|------|------|------|
| `help` | 显示帮助 | `help` |
| `reboot` | 重启设备 | `reboot` |
| `version` | 显示版本 | `version` |

### 网络命令

| 命令 | 说明 | 示例 |
|------|------|------|
| `modem_info` | 模组信息 | `modem_info` |
| `ifconfig` | 网络接口配置 | `ifconfig` |
| `ping` | PING 测试 | `ping 8.8.8.8` |

### PCAPI 命令

| 命令 | 说明 | 示例 |
|------|------|------|
| `pcapi_get` | PCAPI GET 请求 | `pcapi_get /api/system/time` |
| `pcapi_post` | PCAPI POST 请求 | `pcapi_post /api/wifi/ap {...}` |

## 使用场景

### 1. 本地调试

```bash
# 通过串口终端连接
4g_nic> help
4g_nic> modem_info
4g_nic> reboot
```

### 2. PC 软件控制

```python
# Python 示例
import serial

ser = serial.Serial('COM3', 115200)

# 发送 PCAPI 请求
ser.write(b"PCAPI GET /api/system/time\r\n")

# 读取响应
response = ser.readline()
print(response.decode())
```

### 3. 自动化测试

```bash
#!/bin/bash
# 通过串口执行命令
echo "AT+GMR" > /dev/ttyUSB0
cat /dev/ttyUSB0
```

### 4. 远程管理

```
PC Software (Python)
    ↓ USB
USB-UART Converter (CH340/CP2102)
    ↓ UART
ESP32-S3 (Serial CLI + PCAPI)
    ↓ HTTP Loopback
Web Service API
```

## 命令行编辑

### 快捷键

| 按键 | 功能 |
|------|------|
| `↑` / `↓` | 历史命令导航 |
| `←` / `→` | 光标移动 |
| `Tab` | 自动补全 |
| `Backspace` | 删除字符 |
| `Ctrl+C` | 取消当前输入 |
| `Enter` | 执行命令 |

### 历史纪录

- 最多保存 10 条历史命令
- 环形缓冲区管理
- 重启后清除

## 注意事项

### 1. UART 配置

- 确保 UART 参数匹配（波特率、数据位、停止位）
- 常见配置：115200-8-N-1
- 支持硬件流控制（可选）

### 2. PCAPI 时序问题

**重要：** POST 请求的 header 和 body 需要适当延迟：

```python
# PC 端发送
ser.write(header.encode())
ser.flush()
time.sleep(0.05)  # 50ms 延迟
ser.write(body.encode())
ser.flush()
```

**原因：**
- Header 行的 `\r\n` 可能被固件端残留
- 需要时间让固件准备好接收 body
- 避免数据丢失

### 3. 内存管理

- 命令行缓冲区大小有限
- 避免超长命令
- PCAPI body 有最大长度限制

### 4. 并发访问

- CLI 任务独占 UART 读取
- AT 指令共享 UART 时需注意优先级
- PCAPI 请求会阻塞 CLI 输出

### 5. 日志干扰

- 系统日志可能干扰 CLI 显示
- 建议临时关闭日志：`ESP_LOG_LEVEL_NONE`
- 或使用独立 UART 用于 AT/PCAPI

## 故障排查

### 问题：CLI 无响应

**可能原因：**
1. CLI 任务未启动
2. UART 配置错误
3. 任务优先级过低

**解决方法：**
```c
// 检查 CLI 任务
ESP_LOGI(TAG, "CLI task running: %d", 
         esp_task_wdt_status(cli_task_handle) == ESP_OK);
```

### 问题：PCAPI 返回超时

**可能原因：**
1. Body 读取超时
2. 数据格式错误
3. HTTP 服务器未启动

**排查步骤：**
```
1. 检查 header 格式是否正确
2. 检查 body 长度是否匹配
3. 查看固件日志：
   I serial_cli: UART buffer has X bytes available
   I serial_cli: Successfully read POST body[X]
```

### 问题：JSON 解析失败

**常见错误：**
```
E web_service: wifi_ap_post: JSON parse failed at: 
E web_service: wifi_ap_post: raw body len=121
```

**原因：**
1. Header 残留 `\r\n` 被读入 body
2. Body 数据不完整
3. JSON 格式错误

**解决方法：**
- 固件端已修复：消耗 header 残留的 `\r\n`
- PC 端增加发送延迟
- 检查 JSON 格式

## 扩展自定义命令

### 添加新命令

```c
#include "esp_console.h"

static int cmd_mycommand(int argc, char **argv)
{
    printf("My custom command executed\n");
    return 0;
}

void register_my_command(void)
{
    const esp_console_cmd_t cmd = {
        .command = "mycommand",
        .help = "My custom command",
        .hint = NULL,
        .func = &cmd_mycommand,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
```

### 注册时机

在 `serial_cli_start()` 中注册：

```c
void serial_cli_start(void)
{
    // ... 初始化代码 ...
    
    register_my_command();
    
    // ... 启动 CLI 任务 ...
}
```

## PCAPI 调试技巧

### 1. 启用调试日志

```c
// 在 serial_cli.c 中
#define PCAPI_DEBUG 1
```

### 2. 使用十六进制日志

```c
// 查看接收到的原始数据
ESP_LOG_BUFFER_HEX_LEVEL(TAG, body, body_len, ESP_LOG_INFO);
```

### 3. 手动测试

```bash
# 使用串口终端工具
# 1. 输入：PCAPI GET /api/system/time
# 2. 按 Ctrl+J 发送（\n）
# 3. 观察响应
```

## 性能优化

### 1. UART 缓冲区

```kconfig
CONFIG_UART_ISR_IN_IRAM=y
CONFIG_UART_ISR_IN_IRAM=y
```

### 2. 任务优先级

```c
// CLI 任务优先级
#define CLI_TASK_PRIORITY 5
#define CLI_TASK_STACK_SIZE 4096
```

### 3. 响应优化

- 减少日志输出
- 使用异步响应
- 优化 JSON 序列化

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2024 | 初始版本 |
| 1.1.0 | 2024-04 | 修复 PCAPI body 读取问题 |

## 相关文件

```
components/serial_cli/
├── CMakeLists.txt          # CMake 构建配置
├── serial_cli.c            # 主实现文件
└── include/
    └── serial_cli.h        # 公共头文件
```

## 相关文档

- [PCSoftware/](../../PCSoftware/) - PC 软件实现
- [doc/ble_protocol.md](../../doc/ble_protocol.md) - BLE 协议文档

## 许可证

Apache-2.0
