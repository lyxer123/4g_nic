# Router AT 组件

## 功能概述

Router AT 组件实现了类似 ESP-AT 的指令集，通过串口提供类 AT 命令接口，用于设备控制、状态查询和网络管理。该组件支持独立 UART 端口或与 console 共享 UART。

### 主要功能

- 类 ESP-AT 指令集
- 支持独立 UART 或共享 Console UART
- 设备信息查询（芯片、内存、版本等）
- 网络状态查询（4G模组、以太网、WiFi等）
- 工作模式管理
- 网络诊断工具（PING等）
- **WiFi 扫描和STA配置**（新增）
- NVS 持久化配置

## 支持的 AT 指令

### 基础指令

| 指令 | 说明 | 示例 |
|------|------|------|
| `AT` | 测试指令 | `AT` → `OK` |
| `ATE0/ATE1` | 回显关闭/开启 | `ATE0` |
| `AT+GMR` | 固件版本 | `AT+GMR` → `4G_NIC Apr 17 2026 00:16:34` |
| `AT+IDF` | ESP-IDF 版本 | `AT+IDF` |
| `AT+CHIP` | 芯片信息 | `AT+CHIP` → `ESP32-S3` |
| `AT+MEM` | 内存信息 | `AT+MEM` → Free/Min heap |
| `AT+RST` | 重启设备 | `AT+RST` |
| `AT+CMD` | 列出所有指令 | `AT+CMD` |
| `AT+TIME` | 系统时间查询/设置 | `AT+TIME` 或 `AT+TIME=2026-04-17 01:00:00` |
| `AT+SYSRAM` | 系统 RAM 信息 | `AT+SYSRAM` |

### 路由器相关

| 指令 | 说明 | 示例 |
|------|------|------|
| `AT+ROUTER` | 路由器状态 | `AT+ROUTER` |
| `AT+MODE?` | 查询工作模式 | `AT+MODE?` |
| `AT+MODESET=<id>` | 设置工作模式 | `AT+MODESET=0` → 提示配置 |
| `AT+PING` | PING 测试 | `AT+PING=8.8.8.8` |

### 4G 模组相关

| 指令 | 说明 | 示例 |
|------|------|------|
| `AT+MODEMINFO` | 4G模组信息 | `AT+MODEMINFO` |
| `AT+MODEMTIME` | 模组网络时间 | `AT+MODEMTIME` → `26/04/16,17:22:07+32` |

### W5500 以太网

| 指令 | 说明 | 示例 |
|------|------|------|
| `AT+W5500` | W5500状态 | `AT+W5500` |
| `AT+W5500IP` | W5500 IP信息 | `AT+W5500IP` |

### USB 4G 模组

| 指令 | 说明 | 示例 |
|------|------|------|
| `AT+USB4G` | USB 4G状态 | `AT+USB4G` |
| `AT+USB4GIP` | USB 4G IP信息 | `AT+USB4GIP` |

### 网络检测

| 指令 | 说明 | 示例 |
|------|------|------|
| `AT+NETCHECK` | 网络连通性检测 | `AT+NETCHECK` |

### WiFi 配置（新增）

| 指令 | 说明 | 示例 |
|------|------|------|
| `AT+WIFISCAN` | 扫描WiFi网络 | `AT+WIFISCAN` |
| `AT+WIFISTA?` | 查询STA配置 | `AT+WIFISTA?` |
| `AT+WIFISTA=<ssid>,<pwd>` | 设置STA配置 | `AT+WIFISTA="MyWiFi","12345678"` |

## 快速开始

### 初始化

```c
#include "router_at.h"

void app_main(void)
{
    // 其他初始化...
    
    // 启动 AT 指令服务
    router_at_start();
}
```

### API 接口

#### `router_at_start()`

启动 AT 子系统。

**函数原型：**
```c
void router_at_start(void);
```

**说明：**
- 根据配置创建独立 UART 任务或注册行处理钩子
- 自动检测 AT UART 与 Console UART 是否相同

#### `router_at_try_handle_line()`

当 AT 与 Console 共享 UART 时，尝试处理 AT 指令。

**函数原型：**
```c
bool router_at_try_handle_line(const char *line, void (*write_bytes)(const void *data, size_t len));
```

**参数：**
- `line`: 接收到的命令字符串（已去除首尾空白）
- `write_bytes`: 响应数据写入回调函数

**返回值：**
- `true`: 该行是 AT 指令，已处理
- `false`: 不是 AT 指令，交给 CLI 处理

#### `router_at_effective_uart_num()`

获取 AT 使用的 UART 端口号。

**函数原型：**
```c
uint8_t router_at_effective_uart_num(void);
```

**返回值：**
- UART 端口号（0, 1, 或 2）

#### `router_at_is_shared_with_console()`

检查 AT 是否与 Console 共享 UART。

**函数原型：**
```c
bool router_at_is_shared_with_console(void);
```

**返回值：**
- `true`: 共享 UART
- `false`: 独立 UART

#### `router_at_set_uart_num_nvs()`

持久化 AT UART 端口配置到 NVS。

**函数原型：**
```c
esp_err_t router_at_set_uart_num_nvs(uint8_t uart_num);
```

**参数：**
- `uart_num`: UART 端口号（0-2）

**返回值：**
- `ESP_OK`: 保存成功
- 其他 ESP_ERR: 保存失败

**说明：**
- 配置在重启后生效
- 用于生产测试和工具，无需重新烧录固件

## 依赖关系

### 硬件依赖

- UART 端口（独立或共享）
- 可选：4G 模组（用于 AT+MODEMINFO 等指令）
- 可选：W5500 以太网芯片
- 可选：USB 4G 模组

### ESP-IDF 组件依赖

- `driver` (UART 驱动)
- `nvs_flash` (配置存储)
- `esp_wifi` (WiFi 相关)
- `esp_eth` (以太网相关)
- `esp_modem` (4G 模组)
- `iot_bridge` (桥接框架)

### 项目组件依赖

- `system` (系统管理)
- `serial_cli` (串口 CLI，共享 UART 时)

### CMake 配置

```cmake
idf_component_register(
    SRCS "router_at.c"
    INCLUDE_DIRS "include"
    REQUIRES driver nvs_flash esp_wifi esp_eth
    PRIV_REQUIRES system serial_cli
)
```

## 配置选项

### menuconfig 配置

```
Component config --->
    Router AT Configuration --->
        [*] Enable AT command
        AT UART Port (UART0) --->
        AT Baud Rate (115200) --->
        AT Command Terminator (\r\n) --->
```

### NVS 配置

| Namespace | Key | 类型 | 说明 |
|-----------|-----|------|------|
| `router_at` | `uart_num` | `uint8_t` | AT UART 端口号 |

## UART 工作模式

### 模式 1：独立 UART

```
PC ─→ USB-UART ─→ ESP32 UART1 (AT专用)
                     ↓
              Router AT Task
```

**优点：**
- 不干扰 Console 调试
- 可独立配置波特率
- 适合生产环境

### 模式 2：共享 UART

```
PC ─→ USB-UART ─→ ESP32 UART0 (Console + AT)
                     ↓
              Serial CLI ─→ Router AT Hook
```

**优点：**
- 节省 UART 资源
- 开发调试方便
- 适合开发阶段

## 指令处理流程

```
1. 接收一行命令（\r\n 结尾）
   ↓
2. 去除首尾空白字符
   ↓
3. 检查是否以 "AT" 开头
   ↓
4. 是指令 → 解析并执行 → 返回响应
   ↓
5. 不是指令 → 返回 false → 交给 CLI 处理
```

## 响应格式

### 成功响应

```
AT+GMR
4G_NIC Apr 17 2026 00:16:34
OK
```

### 错误响应

```
AT+INVALID
ERROR
```

### 信息查询

```
AT+MODE?
+MODE:2
OK
```

## 使用场景

### 1. 生产测试

```bash
# 查询设备信息
AT+GMR
AT+CHIP
AT+MEM

# 测试网络
AT+PING=8.8.8.8
AT+MODEMINFO
```

### 2. 设备调试

```bash
# 查看状态
AT+ROUTER
AT+MODE?

# 切换模式
AT+MODE=2
AT+RST
```

### 3. 网络诊断

```bash
# 检查网络
AT+NETCHECK

# PING 测试
AT+PING=192.168.1.1
```

## 注意事项

### 1. 指令格式

- 所有指令以 `AT` 开头
- 不区分大小写（建议大写）
- 以 `\r\n` 结尾
- 查询使用 `?`，设置使用 `=`

### 2. UART 冲突

- 独立 UART 模式下，避免同时访问
- 共享 UART 模式下，AT 指令优先处理

### 3. NVS 配置

- UART 端口修改需要重启生效
- 使用 `router_at_set_uart_num_nvs()` 动态配置

### 4. 4G 模组指令

- 需要模组在线才能查询
- 返回信息依赖模组固件版本

## 故障排查

### 问题：AT 指令无响应

**可能原因：**
1. AT 服务未启动
2. UART 配置错误
3. 波特率不匹配

**解决方法：**
```c
// 检查 AT UART
ESP_LOGI(TAG, "AT UART: %d", router_at_effective_uart_num());
ESP_LOGI(TAG, "Shared: %d", router_at_is_shared_with_console());
```

### 问题：返回 ERROR

**检查项：**
1. 指令拼写是否正确
2. 参数格式是否正确
3. 模块是否已初始化

## 扩展现有指令

添加新的 AT 指令：

```c
// 在 router_at.c 的 at_handle_query() 中添加
if (strcmp(name, "MYCMD") == 0) {
    at_write_fmt(write_bytes, "\r\n+MYCMD:result\r\n");
    at_ok(write_bytes);
    return;
}
```

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2024 | 初始版本 |

## 相关文件

```
components/router_at/
├── CMakeLists.txt          # CMake 构建配置
├── Kconfig                 # menuconfig 配置
├── router_at.c             # 主实现文件
└── include/
    └── router_at.h         # 公共头文件
```

## 相关文档

- [AT指令说明.md](../../at指令说明.md) - 完整 AT 指令文档
- [AT指令实现.md](../../at指令实现.md) - 实现细节
- [PCSoftware/AT指令说明.md](../../PCSoftware/AT指令说明.md) - PC 软件 AT 测试指南

## 许可证

Apache-2.0
