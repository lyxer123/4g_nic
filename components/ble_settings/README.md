# BLE Settings 组件

## 功能概述

BLE Settings 组件提供蓝牙低功耗 (BLE) 外设服务，用于通过手机APP配置设备参数。该组件实现了 vendor-specific GATT 服务（UUID: 0xFF50），支持通过 JSON 格式的命令/响应进行设备配置。

### 主要功能

- BLE 外设模式（GATT Server）
- Vendor Service UUID: 0xFF50
- JSON 格式命令交互
- 支持设备配置参数传输
- 单次初始化保护（多次调用安全）

## 快速开始

### 初始化

```c
#include "ble_settings.h"

void app_main(void)
{
    // 其他初始化...
    
    // 启动 BLE 配置服务
    esp_err_t ret = ble_settings_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start BLE settings: %s", esp_err_to_name(ret));
    }
}
```

### API 接口

#### `ble_settings_start()`

启动 BLE 外设服务。

**函数原型：**
```c
esp_err_t ble_settings_start(void);
```

**返回值：**
- `ESP_OK`: 成功启动
- 其他 ESP_ERR: 启动失败

**特性：**
- 安全调用：多次调用不会重复初始化
- 后续调用直接返回 `ESP_OK`

## 通信协议

### GATT Service 结构

```
Service UUID: 0xFF50
├── Characteristic: Command (Write)
└── Characteristic: Response (Notify/Indicate)
```

### 数据格式

**命令格式（JSON）：**
```json
{
    "cmd": "set_wifi",
    "ssid": "MyWiFi",
    "password": "12345678"
}
```

**响应格式（JSON）：**
```json
{
    "status": "success",
    "message": "WiFi config updated"
}
```

## 依赖关系

### 硬件依赖

- ESP32 系列芯片（内置 BLE）
- BLE 天线

### ESP-IDF 组件依赖

- `bt` (Bluedroid 蓝牙协议栈)
- `nvs_flash` (参数存储)
- `esp_event` (事件处理)

### CMake 配置

```cmake
idf_component_register(
    SRCS "ble_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash esp_event
)
```

## 配置选项

通过 `menuconfig` 配置：

```
Component config --->
    Bluetooth --->
        [*] Bluetooth enabled
        Choose Bluetooth Host Controller (Bluedroid) --->
        Bluedroid Mode (BLE only) --->
```

## 使用场景

### 1. 手机APP配网

```
手机APP → BLE → ESP32 → 配置WiFi参数
```

### 2. 设备参数设置

- WiFi SSID/Password
- 工作模式配置
- 网络参数设置
- 设备信息查询

### 3. 出厂配置

- 生产测试
- 初始参数写入
- 设备绑定

## 注意事项

### 1. 内存占用

- Bluedroid 协议栈占用约 100KB RAM
- 建议在不需要时关闭 BLE

### 2. 初始化顺序

```
1. NVS 初始化
2. WiFi 初始化（可选）
3. BLE Settings 启动
```

### 3. 与 WiFi 共存

ESP32 支持 BLE 和 WiFi 共存，但需要注意：
- 天线共享（时间分片）
- 可能影响 WiFi 吞吐量
- 建议配网完成后关闭 BLE

## 故障排查

### 问题：BLE 无法启动

**可能原因：**
1. Bluedroid 未启用
2. NVS 未初始化
3. 内存不足

**解决方法：**
```c
// 检查 BLE 配置
ESP_LOGI(TAG, "BLE enabled: %d", esp_bt_controller_get_status());

// 检查内存
ESP_LOGI(TAG, "Free heap: %d", esp_get_free_heap_size());
```

### 问题：手机无法连接

**检查项：**
1. BLE 服务是否启动成功
2. Service UUID 是否正确（0xFF50）
3. 手机蓝牙权限是否开启

## 示例代码

完整示例请参考：
- `main/app_main.c` - 主程序初始化
- PCSoftware 中的蓝牙配网实现

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2024 | 初始版本 |

## 相关文件

```
components/ble_settings/
├── CMakeLists.txt          # CMake 构建配置
├── ble_settings.c          # 主实现文件
└── include/
    └── ble_settings.h      # 公共头文件
```

## 许可证

Apache-2.0
