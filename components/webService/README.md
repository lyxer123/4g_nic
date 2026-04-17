# Web Service 组件

## 功能概述

Web Service 组件提供了基于 ESP-IDF `esp_http_server` 的 HTTP/REST API 服务，用于设备配置、状态查询和系统管理。该组件实现了完整的 Web UI 后端，并支持通过 PCAPI 协议进行串口 HTTP 桥接。

### 主要功能

- HTTP/REST API 服务
- Web UI 静态文件服务
- 设备配置管理（WiFi、网络、系统等）
- NVS 持久化存储
- PC Loopback HTTP（串口 HTTP 桥接）
- 日志环缓冲区
- 工作模式管理
- 时间同步

## REST API 接口

### 系统管理

| 端点 | 方法 | 说明 | 示例 |
|------|------|------|------|
| `/api/system/time` | GET | 获取系统时间 | `{"system_time":"2026-04-17 01:26:52"}` |
| `/api/system/sync_time` | POST | 同步时间 | `{"local_timestamp_ms":1776360410960}` |
| `/api/system/reboot` | POST | 重启设备 | - |
| `/api/system/info` | GET | 系统信息 | `{"firmware_version":"..."}` |

### WiFi 配置

| 端点 | 方法 | 说明 | 示例 |
|------|------|------|------|
| `/api/wifi/ap` | GET | 获取 AP 配置 | `{"ssid":"ESP_D7B119",...}` |
| `/api/wifi/ap` | POST | 设置 AP 配置 | `{"wifi_enabled":true,"ssid":"...",...}` |
| `/api/wifi/sta` | GET | 获取 STA 配置 | `{"ssid":"MyWiFi",...}` |
| `/api/wifi/sta` | POST | 设置 STA 配置 | `{"ssid":"MyWiFi","password":"..."}` |

### 网络配置

| 端点 | 方法 | 说明 | 示例 |
|------|------|------|------|
| `/api/network/config` | GET | 获取网络配置 | `{"work_mode_id":2,...}` |
| `/api/network/config` | POST | 设置网络配置 | `{"work_mode_id":2,"wan_type":1,...}` |
| `/api/network/status` | GET | 网络状态 | `{"wan_connected":true,...}` |

### 日志管理

| 端点 | 方法 | 说明 | 示例 |
|------|------|------|------|
| `/api/log` | GET | 获取系统日志 | 日志环缓冲区内容 |

## 快速开始

### 初始化

```c
#include "web_service.h"

void app_main(void)
{
    // NVS 初始化
    esp_storage_init();
    
    // 网络接口初始化
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 恢复 SoftAP 配置
    web_softap_restore_from_nvs();
    
    // 启动 Web 服务
    ESP_ERROR_CHECK(web_service_start());
}
```

### API 接口

#### `web_service_start()`

启动 HTTP Web 服务。

**函数原型：**
```c
esp_err_t web_service_start(void);
```

**返回值：**
- `ESP_OK`: 成功启动
- 其他 ESP_ERR: 启动失败

**说明：**
- 创建 HTTP 服务器（默认端口 80）
- 注册所有 REST API 端点
- 启动静态文件服务
- 创建日志环缓冲区

#### `web_service_stop()`

停止 Web 服务。

**函数原型：**
```c
esp_err_t web_service_stop(void);
```

**返回值：**
- `ESP_OK`: 成功停止
- 其他 ESP_ERR: 停止失败

#### `web_service_is_running()`

检查 Web 服务是否运行。

**函数原型：**
```c
bool web_service_is_running(void);
```

**返回值：**
- `true`: 服务运行中
- `false`: 服务未运行

#### `web_softap_restore_from_nvs()`

从 NVS 恢复 SoftAP 配置。

**函数原型：**
```c
void web_softap_restore_from_nvs(void);
```

**调用时机：**
- 在 bridge WiFi 初始化之后调用
- 确保使用 WIFI_STORAGE_RAM 模式

#### `web_service_get_work_mode_u8()`

从 NVS 读取工作模式 ID。

**函数原型：**
```c
esp_err_t web_service_get_work_mode_u8(uint8_t *out);
```

**参数：**
- `out`: 输出工作模式 ID

**返回值：**
- `ESP_OK`: 读取成功
- 其他 ESP_ERR: 读取失败

#### `web_service_apply_work_mode_id()`

验证、持久化并应用工作模式。

**函数原型：**
```c
esp_err_t web_service_apply_work_mode_id(uint8_t mode_id);
```

**参数：**
- `mode_id`: 工作模式 ID

**返回值：**
- `ESP_OK`: 应用成功
- 其他 ESP_ERR: 应用失败

**说明：**
- 验证模式 ID 合法性
- 保存到 NVS
- 调度延迟应用（`system_mode_manager_apply`）
- 更新 Web UI 配置

#### `web_pc_loopback_http()`

通过 HTTP 客户端访问本地服务器（127.0.0.1）。

**函数原型：**
```c
esp_err_t web_pc_loopback_http(const char *method,
                               const char *path,
                               const char *content_type,
                               const char *body,
                               size_t body_len,
                               int *status_code,
                               char **resp_out,
                               size_t *resp_len);
```

**参数：**
- `method`: HTTP 方法（GET/POST/DELETE）
- `path`: 请求路径
- `content_type`: 内容类型（POST 时可选）
- `body`: POST 数据（可选）
- `body_len`: POST 数据长度
- `status_code`: 输出 HTTP 状态码
- `resp_out`: 输出响应数据（需 free）
- `resp_len`: 响应数据长度

**使用场景：**
- PCAPI 协议实现
- 串口 HTTP 桥接
- 内部服务调用

**示例：**
```c
char *resp = NULL;
size_t resp_len = 0;
int status = 0;

esp_err_t ret = web_pc_loopback_http(
    "GET",
    "/api/system/time",
    NULL,
    NULL,
    0,
    &status,
    &resp,
    &resp_len
);

if (ret == ESP_OK && status == 200) {
    ESP_LOGI(TAG, "Response: %.*s", resp_len, resp);
}

free(resp);
```

## 依赖关系

### ESP-IDF 组件依赖

- `esp_http_server` (HTTP 服务器)
- `nvs_flash` (配置存储)
- `esp_wifi` (WiFi 支持)
- `esp_eth` (以太网支持)
- `json` (JSON 解析 - cJSON)
- `esp_timer` (定时器)

### 项目组件依赖

- `system` (系统管理)
- `serial_cli` (PCAPI 支持)
- `iot_bridge` (桥接框架)

### CMake 配置

```cmake
idf_component_register(
    SRCS 
        "src/web_service.c"
        "src/web_pc_loopback.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server nvs_flash esp_wifi json
    PRIV_REQUIRES system serial_cli
)
```

## 配置选项

### menuconfig 配置

```
Component config --->
    Web Service Configuration --->
        [*] Enable Web Service
        HTTP Server Port (80) --->
        Max HTTP Request Size (4096) --->
        [*] Enable Static File Service
        Web Root Path (/spiffs/www) --->
        Log Ring Buffer Size (4096) --->
```

## 请求处理流程

### GET 请求

```
Client ─→ HTTP GET /api/system/time
           ↓
     web_service.c
     uri_system_time_get()
           ↓
     获取系统时间
           ↓
     cJSON 序列化
           ↓
     HTTP Response 200
     {"system_time":"2026-04-17 01:26:52"}
```

### POST 请求

```
Client ─→ HTTP POST /api/wifi/ap
           ↓
     接收 Body (JSON)
           ↓
     cJSON_Parse()
           ↓
     验证参数
           ↓
     更新配置 + NVS 保存
           ↓
     HTTP Response 200
     {"status":"success"}
```

## 数据格式

### 请求示例

**设置 AP 配置：**
```json
POST /api/wifi/ap
{
    "wifi_enabled": true,
    "ssid": "ESP_D7B119",
    "encryption_mode": "WPA2-PSK",
    "password": "12345678",
    "hidden_ssid": false
}
```

**设置网络配置：**
```json
POST /api/network/config
{
    "work_mode_id": 2,
    "wan_type": 1,
    "lan": {
        "ip": "192.168.4.1",
        "mask": "255.255.255.0",
        "dhcp_enabled": true,
        "dhcp_start": "192.168.4.100",
        "dhcp_end": "192.168.4.199",
        "dns1": "8.8.8.8",
        "dns2": "114.114.114.114"
    }
}
```

### 响应示例

**成功：**
```json
{
    "status": "success"
}
```

**失败：**
```json
{
    "status": "error",
    "message": "invalid json"
}
```

**查询：**
```json
GET /api/system/time
→ 200 OK
{
    "system_time": "2026-04-17 01:26:52",
    "firmware_version": "4G_NIC Apr 17 2026 00:16:34",
    "timezone": "(GMT+08:00) Asia/Shanghai"
}
```

## 日志环缓冲区

### 功能

- 循环缓冲最新日志
- Web API 可读取
- 固定大小（可配置）
- 线程安全

### API

```c
// 写入日志
log_ring_fmt("[WIFI] AP started");

// 通过 API 读取
GET /api/log
→ 返回最新日志内容
```

## 使用场景

### 1. Web UI 配置

```
Browser ─→ http://192.168.4.1
              ↓
         Web Service (HTTP Server)
              ↓
         REST API Handlers
              ↓
         NVS / WiFi / Network
```

### 2. PC 软件控制（PCAPI）

```
PC Software (Python)
    ↓ USB
USB-UART Converter
    ↓ UART
Serial CLI (PCAPI Parser)
    ↓
web_pc_loopback_http()
    ↓ HTTP Loopback
Web Service API
    ↓
Response via UART
```

### 3. 移动端 BLE 配置

```
Mobile APP (BLE)
    ↓ BLE
BLE Settings Component
    ↓
Web Service API (内部调用)
    ↓
NVS 保存配置
```

## 注意事项

### 1. 内存管理

- cJSON 对象需要及时释放
- HTTP 响应数据需要 free
- 避免内存泄漏

```c
// 正确释放
cJSON *root = cJSON_Parse(body);
// ... 使用 ...
cJSON_Delete(root);
```

### 2. JSON 解析

- 检查解析结果
- 验证必需字段
- 处理解析错误

```c
cJSON *root = cJSON_Parse(body);
if (!root) {
    return send_json(req, 400, "{\"status\":\"error\",\"message\":\"invalid json\"}");
}

cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
if (!cJSON_IsString(ssid)) {
    cJSON_Delete(root);
    return send_json(req, 400, "{\"status\":\"error\",\"message\":\"ssid required\"}");
}
```

### 3. NVS 操作

- 检查 NVS 空间
- 处理写入失败
- 使用事务保证一致性

### 4. HTTP 响应

- 设置正确的 Content-Type
- 使用适当的 HTTP 状态码
- 避免超长响应

```c
httpd_resp_set_type(req, "application/json");
httpd_resp_set_status(req, "200 OK");
httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
```

### 5. 并发访问

- HTTP 服务器多线程
- 使用互斥锁保护共享资源
- 避免长时间阻塞

## 故障排查

### 问题：HTTP 服务无法启动

**可能原因：**
1. 端口被占用
2. 内存不足
3. WiFi 未初始化

**解决方法：**
```c
ESP_LOGI(TAG, "Free heap: %d", esp_get_free_heap_size());
ESP_LOGI(TAG, "WiFi connected: %d", wifi_is_connected());
```

### 问题：JSON 解析失败

**日志：**
```
E web_service: JSON parse failed at: 
E web_service: raw body len=121
```

**原因：**
1. Body 数据不完整
2. 包含非法字符
3. 格式错误

**调试：**
```c
// 打印十六进制数据
ESP_LOG_BUFFER_HEX_LEVEL(TAG, body, body_len, ESP_LOG_INFO);
```

### 问题：NVS 写入失败

**可能原因：**
1. NVS 空间不足
2. 键名过长
3. 值类型不匹配

**解决方法：**
```bash
# 擦除 NVS
idf.py erase-flash
```

## 添加新的 API 端点

### 1. 实现处理函数

```c
static esp_err_t uri_my_custom_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "message", "Hello World");
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    
    return ESP_OK;
}
```

### 2. 注册端点

```c
// 在 web_service_start() 中
httpd_uri_t my_uri = {
    .uri = "/api/custom",
    .method = HTTP_GET,
    .handler = uri_my_custom_get,
    .user_ctx = NULL
};
httpd_register_uri_handler(server, &my_uri);
```

## 性能优化

### 1. 使用 cJSON_PrintUnformatted

```c
// 快速（无空格）
char *json = cJSON_PrintUnformatted(root);

// 慢速（格式化）
char *json = cJSON_Print(root);
```

### 2. 减少内存分配

```c
// 预分配缓冲区
char resp_buf[512];
snprintf(resp_buf, sizeof(resp_buf), "{\"status\":\"success\"}");
httpd_resp_sendstr(req, resp_buf);
```

### 3. 启用 HTTP 服务器优化

```kconfig
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG_HTTPD_MAX_URI_LEN=512
CONFIG_HTTPD_ERR_RESP_NO_DELAY=y
```

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2024 | 初始版本 |
| 1.1.0 | 2024-04 | 增强 PCAPI 支持 |

## 相关文件

```
components/webService/
├── CMakeLists.txt          # CMake 构建配置
├── include/
│   └── web_service.h       # 公共头文件
└── src/
    ├── web_service.c       # 主实现文件（REST API）
    └── web_pc_loopback.c   # PC Loopback HTTP 实现
```

## 相关文档

- [webPage/](../../webPage/) - Web UI 前端文件
- [doc/webDeploy/](../../doc/webDeploy/) - Web 部署工具
- [PCSoftware/](../../PCSoftware/) - PC 软件实现

## 许可证

Apache-2.0
