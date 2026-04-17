# System 组件

## 功能概述

System 组件是 4G NIC 路由器固件的核心系统管理模块，包含硬件检测、模式管理、桥接运行、网络诊断、稳定性管理等多个子模块。该组件负责整个系统的初始化、配置管理、运行时监控和故障恢复。

### 子模块清单

| 子模块 | 文件名 | 功能 |
|--------|--------|------|
| Bridge Runtime | `system_bridge_runtime.c` | 网桥运行时管理 |
| Mode Manager | `system_mode_manager.c` | 工作模式管理 |
| HW Presence | `system_hw_presence.c` | 硬件存在性检测 |
| USB CAT1 Detect | `system_usb_cat1_detect.c` | USB 4G 模组检测 |
| W5500 Detect | `system_w5500_detect.c` | W5500 以太网检测 |
| WiFi Dual Connect | `system_wifi_dual_connect.c` | WiFi 双连接管理 |
| STA Baidu Probe | `system_sta_baidu_probe.c` | STA 连通性探测 |
| ETH Uplink Debug | `system_eth_uplink_debug.c` | 以太网链路调试 |
| Stability | `system_stability.c` | 系统稳定性管理 |
| NAPT Compat | `system_napt_compat.c` | NAPT 兼容性 |

## 子模块详细说明

---

## 1. System Bridge Runtime

### 功能

管理 esp-iot-bridge 的运行时行为，包括：
- 网桥启动和停止
- 网络接口协调
- NAPT 转发配置

### API

```c
#include "system_bridge_runtime.h"

// 初始化网桥
esp_err_t system_bridge_init_netifs_from_hw(void);

// 应用配置
esp_err_t system_bridge_apply_config(void);
```

### 依赖

- `iot_bridge` (esp-iot-bridge 库)
- `esp_netif` (网络接口)

---

## 2. System Mode Manager

### 功能

管理工作模式，支持多种路由器模式：
- 模式 ID 定义和验证
- 模式切换和持久化
- 延迟应用机制
- 模式状态查询

### 工作模式

| 模式 ID | 名称 | 说明 |
|---------|------|------|
| 0 | WiFi Router | WiFi STA WAN + SoftAP LAN |
| 1 | Ethernet Router | W5500 WAN + SoftAP LAN |
| 2 | USB 4G Router | USB CAT1 WAN + SoftAP LAN |
| 3 | 4G Router | PPP 4G WAN + SoftAP LAN |

### API

```c
#include "system_mode_manager.h"

// 记录启动计划
void system_mode_manager_log_startup_plan(void);

// 应用保存的模式或硬件默认
esp_err_t system_mode_manager_apply_saved_or_hw_default(void);

// 应用指定模式
esp_err_t system_mode_manager_apply_mode(uint8_t mode_id);

// 获取当前模式
uint8_t system_mode_manager_get_current_mode(void);
```

### 配置流程

```
1. 读取 NVS 中的 work_mode_id
   ↓
2. 验证模式 ID 合法性
   ↓
3. 记录启动计划日志
   ↓
4. 桥接初始化后应用模式
   ↓
5. 配置对应的 WAN/LAN 接口
```

### NVS 存储

| Namespace | Key | 类型 | 说明 |
|-----------|-----|------|------|
| `bridge_ui` | `work_mode` | `uint8_t` | 工作模式 ID |

---

## 3. System HW Presence

### 功能

在桥接初始化前检测硬件存在性：
- W5500 以太网芯片检测
- USB 4G 模组检测
- 硬件初始化状态记录

### API

```c
#include "system_hw_presence.h"

// 桥接前硬件探测
void system_hw_presence_probe_before_bridge(void);

// 检查 W5500 是否存在
bool system_hw_w5500_present(void);

// 检查 USB 4G 是否存在
bool system_hw_usb_cat1_present(void);
```

### 检测时机

```
系统启动
   ↓
HW Presence Probe (桥接前)
   ↓
Bridge Init Netifs
   ↓
Mode Manager Apply
```

---

## 4. System USB CAT1 Detect

### 功能

USB CDC 4G 模组自动检测：
- USB 设备枚举
- VID/PID 匹配
- 模组就绪检测
- AT 命令测试

### 支持的模组

| 厂商 | VID | PID | 型号 |
|------|-----|-----|------|
| Fibocom | 0x2D46 | 0x0101 | FM350 |
| Quectel | 0x2C7C | 多种 | EC20/EG25 等 |
| SIMCOM | 0x1E0E | 多种 | SIM7600 等 |

### API

```c
#include "system_usb_cat1_detect.h"

// 检测 USB CAT1 模组
bool system_usb_cat1_detect(void);

// 获取模组信息
esp_err_t system_usb_cat1_get_info(char *buf, size_t len);

// 测试 AT 命令
esp_err_t system_usb_cat1_at_test(void);
```

### 检测流程

```
1. 扫描 USB 设备
   ↓
2. 匹配 VID/PID
   ↓
3. 打开 CDC ACM 接口
   ↓
4. 发送 AT 命令测试
   ↓
5. 返回检测结果
```

---

## 5. System W5500 Detect

### 功能

W5500 以太网芯片检测：
- SPI 总线检测
- 芯片 ID 读取
- 网络连接状态

### API

```c
#include "system_w5500_detect.h"

// 检测 W5500
bool system_w5500_detect(void);

// 获取 W5500 状态
esp_err_t system_w5500_get_status(w5500_status_t *status);
```

### 硬件接口

```
ESP32-S3 ─→ SPI ─→ W5500
   ↓
MOSI, MISO, SCK, CS
   ↓
INT (可选)
RST
```

---

## 6. System WiFi Dual Connect

### 功能

WiFi 双连接管理：
- 同时支持 STA + SoftAP
- 连接优先级管理
- 自动切换
- 信号质量监控

### 工作模式

```
模式 A: STA 连接正常
Internet ← WiFi STA ← AP
                 ↓
           SoftAP → Clients

模式 B: STA 断开
Clients ← SoftAP (本地网络)
```

### API

```c
#include "system_wifi_dual_connect.h"

// 初始化双连接
void system_wifi_dual_connect_init(void);

// 检查 STA 连接状态
bool system_wifi_sta_connected(void);

// 获取信号强度
int8_t system_wifi_sta_rssi(void);
```

### 配置

```kconfig
CONFIG_SYSTEM_WIFI_DUAL_CONNECT=y
CONFIG_SYSTEM_WIFI_STA_RECONNECT_INTERVAL=5
CONFIG_SYSTEM_WIFI_RSSI_THRESHOLD=-80
```

---

## 7. System STA Baidu Probe

### 功能

STA 网络连通性探测：
- PING 百度服务器
- 周期性检测
- 连接质量评估
- 自动重连触发

### API

```c
#include "system_sta_baidu_probe.h"

// 初始化探测
void system_sta_baidu_probe_init(void);

// 获取探测结果
bool system_sta_baidu_probe_result(void);
```

### 探测策略

```
每 60 秒探测一次
   ↓
PING www.baidu.com
   ↓
成功 → 连接正常
失败 → 标记异常
连续 3 次失败 → 触发重连
```

---

## 8. System ETH Uplink Debug

### 功能

以太网上行链路调试：
- 链路状态监控
- 数据包统计
- 错误计数
- 诊断日志

### API

```c
#include "system_eth_uplink_debug.h"

// 初始化调试
void system_eth_uplink_debug_init(void);

// 打印调试信息
void system_eth_uplink_debug_dump(void);
```

### 调试信息

```
Ethernet Uplink Status:
  Link: UP
  Speed: 100Mbps
  Duplex: Full
  TX Packets: 12345
  RX Packets: 67890
  TX Errors: 0
  RX Errors: 0
```

---

## 9. System Stability

### 功能

系统稳定性管理：
- 看门狗监控
- 内存泄漏检测
- 任务状态监控
- 自动恢复机制

### API

```c
#include "system_stability.h"

// 初始化稳定性管理
esp_err_t system_stability_init(void);

// 检查系统健康状态
bool system_stability_check(void);

// 获取运行时间
uint32_t system_stability_uptime(void);
```

### 监控项

| 监控项 | 检查间隔 | 动作 |
|--------|----------|------|
| Free Heap | 30s | < 20KB 告警 |
| Task Stack | 60s | 高水位 < 20% 告警 |
| WiFi 连接 | 10s | 断开重连 |
| PPP 连接 | 10s | 断开重连 |

---

## 10. System NAPT Compat

### 功能

NAPT (Network Address Port Translation) 兼容性处理：
- NAPT 启用/禁用
- 多 WAN 场景适配
- 转发规则管理

### API

```c
#include "system_napt_compat.h"

// 启用 NAPT
esp_err_t system_napt_enable(esp_netif_t *netif);

// 禁用 NAPT
esp_err_t system_napt_disable(esp_netif_t *netif);
```

---

## 依赖关系

### ESP-IDF 组件依赖

- `esp_netif` (网络接口)
- `esp_wifi` (WiFi)
- `esp_eth` (以太网)
- `nvs_flash` (配置存储)
- `esp_timer` (定时器)
- `driver` (SPI/UART)
- `usb` (USB 主机)

### 项目组件依赖

- `router_at` (AT 指令)
- `router_ppp` (PPP 连接)
- `webService` (Web API)
- `iot_bridge` (桥接框架)

### CMake 配置

```cmake
idf_component_register(
    SRCS
        "system_bridge_runtime.c"
        "system_mode_manager.c"
        "system_hw_presence.c"
        "system_usb_cat1_detect.c"
        "system_w5500_detect.c"
        "system_wifi_dual_connect.c"
        "system_sta_baidu_probe.c"
        "system_eth_uplink_debug.c"
        "system_stability.c"
        "system_napt_compat.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_wifi esp_eth nvs_flash
    PRIV_REQUIRES router_at router_ppp webService
)
```

## 配置选项

### menuconfig 配置

```
Component config --->
    System Configuration --->
        [*] Enable System Management
        Work Mode Configuration --->
            Default Work Mode (WiFi Router) --->
        Hardware Detection --->
            [*] Enable W5500 Detection
            [*] Enable USB CAT1 Detection
        WiFi Management --->
            [*] Enable Dual Connect
            [*] Enable Baidu Probe
        Stability Management --->
            [*] Enable Watchdog
            Heap Check Interval (30s) --->
```

## 系统启动流程

```
app_main()
   ↓
1. NVS Init
   ↓
2. esp_netif_init()
   ↓
3. system_hw_presence_probe_before_bridge()
   ├─ W5500 Detect
   └─ USB CAT1 Detect
   ↓
4. system_bridge_init_netifs_from_hw()
   ↓
5. system_mode_manager_apply_saved_or_hw_default()
   ↓
6. system_wifi_dual_connect_init()
   ↓
7. web_softap_restore_from_nvs()
   ↓
8. system_sta_baidu_probe_init()
   ↓
9. system_eth_uplink_debug_init()
   ↓
10. system_stability_init()
   ↓
11. web_service_start()
   ↓
12. serial_cli_start()
   ↓
13. router_at_start()
   ↓
14. router_ppp_start()
```

## 使用场景

### 1. 工作模式切换

```c
// 通过 Web API 切换模式
POST /api/network/config
{
    "work_mode_id": 2  // USB 4G Router
}

// 内部调用
web_service_apply_work_mode_id(2);
```

### 2. 硬件检测

```c
// 启动时检测硬件
system_hw_presence_probe_before_bridge();

if (system_hw_w5500_present()) {
    ESP_LOGI(TAG, "W5500 ethernet detected");
}

if (system_hw_usb_cat1_present()) {
    ESP_LOGI(TAG, "USB 4G modem detected");
}
```

### 3. 稳定性监控

```c
// 定期检查系统健康
while (1) {
    if (!system_stability_check()) {
        ESP_LOGW(TAG, "System stability issue detected");
        // 采取恢复措施
    }
    vTaskDelay(pdMS_TO_TICKS(30000));
}
```

## 注意事项

### 1. 初始化顺序

必须严格按照启动流程顺序初始化：
1. NVS → Netif → HW Presence → Bridge → Mode → ...
2. 错误的顺序可能导致配置丢失或硬件异常

### 2. 模式切换

- 模式切换需要重启生效
- 使用延迟应用避免冲突
- 保存配置到 NVS

### 3. 硬件检测

- 检测失败不影响系统启动
- 使用软件默认配置
- 支持热插拔（USB 4G）

### 4. 内存管理

- 定期检查 heap 使用
- 避免内存泄漏
- 大缓冲区使用 PSRAM

### 5. 并发安全

- 使用互斥锁保护共享数据
- 避免在中断中调用阻塞函数
- NVS 操作需要序列化

## 故障排查

### 问题：模式切换失败

**检查：**
```c
ESP_LOGI(TAG, "Current mode: %d", system_mode_manager_get_current_mode());
ESP_LOGI(TAG, "NVS mode: %d", read_mode_from_nvs());
```

### 问题：硬件检测失败

**检查：**
```c
// W5500
ESP_LOGI(TAG, "W5500 present: %d", system_hw_w5500_present());

// USB 4G
ESP_LOGI(TAG, "USB CAT1 present: %d", system_hw_usb_cat1_present());
```

### 问题：系统不稳定

**监控：**
```c
// 检查 heap
ESP_LOGI(TAG, "Free heap: %d", esp_get_free_heap_size());

// 检查任务
ESP_LOGI(TAG, "Min free stack: %d", uxTaskGetStackHighWaterMark(task_handle));
```

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2024 | 初始版本 |
| 1.1.0 | 2024-04 | 增加 USB 4G 检测 |

## 相关文件

```
components/system/
├── CMakeLists.txt              # CMake 构建配置
├── Kconfig                     # menuconfig 配置
├── include/
│   ├── system_bridge_runtime.h
│   ├── system_mode_manager.h
│   ├── system_hw_presence.h
│   ├── system_usb_cat1_detect.h
│   ├── system_w5500_detect.h
│   ├── system_wifi_dual_connect.h
│   ├── system_sta_baidu_probe.h
│   ├── system_eth_uplink_debug.h
│   ├── system_stability.h
│   └── system_napt_compat.h
├── system_bridge_runtime.c
├── system_mode_manager.c
├── system_hw_presence.c
├── system_usb_cat1_detect.c
├── system_w5500_detect.c
├── system_wifi_dual_connect.c
├── system_sta_baidu_probe.c
├── system_eth_uplink_debug.c
├── system_stability.c
└── system_napt_compat.c
```

## 相关文档

- [多wan智能设计.md](../../多wan智能设计.md) - 多 WAN 架构设计
- [路由器解决方案.md](../../路由器解决方案.md) - 路由器方案概述
- [调试.md](../../调试.md) - 调试指南

## 许可证

Apache-2.0
