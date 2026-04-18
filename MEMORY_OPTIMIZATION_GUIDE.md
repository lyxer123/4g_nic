# ESP32-S3 内存优化指南

## 问题诊断

### 启动日志分析
```
[串][08:14:40] I (30187) app_main: Heap before web start: free=6720, min=3112
[串][08:14:40] E (30198) web_service: web_service_start(2622): httpd start failed
[串][08:14:40] W (30199) app_main: Web service task creation failed, continuing without web interface
[串][08:14:40] E (30206) serial_cli: xTaskCreate uart_cli failed
```

**问题**:
1. `free=6720` - 可用堆内存仅6.7KB
2. `min=3112` - 历史最小曾低至3KB
3. HTTPD任务创建失败 - 需要4-8KB堆栈
4. 串口CLI任务创建失败 - 原配置需要12KB堆栈

**根本原因**: 任务堆栈需求 > 可用内存

---

## 已应用的修复

### 1. 串口CLI堆栈减少 (serial_cli.c)
```c
// 修改前
xTaskCreate(uart_cli_task, "uart_cli", 12288, ...)

// 修改后  
xTaskCreate(uart_cli_task, "uart_cli", 4096, ...)
```

### 2. Web服务错误处理 (app_main.c)
```c
// 原来直接abort
ESP_ERROR_CHECK(web_service_start());

// 现在优雅处理
esp_err_t ret = web_service_start();
if (ret == ESP_ERR_HTTPD_TASK) {
    ESP_LOGW(TAG, "Web service task creation failed, continuing...");
}
```

### 3. 新增内存优化配置 (sdkconfig.defaults)

```bash
# HTTP Server: 明确设置堆栈大小
CONFIG_ESP_HTTPD_STACK_SIZE=4096

# LWIP: 减少内存占用
CONFIG_LWIP_MAX_SOCKETS=16              # 原24
CONFIG_LWIP_MAX_ACTIVE_TCP=24             # 原32
CONFIG_LWIP_MAX_LISTENING_TCP=12          # 原16
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=2920      # 原5760
CONFIG_LWIP_TCP_WND_DEFAULT=2920          # 原5760
CONFIG_LWIP_TCPIP_RECVMBOX_SIZE=32        # 原64
CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=4096    # 原6144

# 系统任务堆栈
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=2048  # 原4096
CONFIG_ESP_MAIN_TASK_STACK_SIZE=4096          # 原5120
CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=2048   # 原4096

# WiFi缓冲区
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=12
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=12
CONFIG_ESP_WIFI_TX_BA_WIN=6
CONFIG_ESP_WIFI_RX_BA_WIN=6
```

---

## 重新编译步骤 (启用PSRAM)

```bash
cd F:\github\4g_nic

# 1. 清理构建 (必须！清理旧配置缓存)
idf.py fullclean

# 2. 重新设置目标芯片 (应用新的sdkconfig.defaults，包含PSRAM配置)
idf.py set-target esp32s3

# 3. 编译
idf.py build

# 4. 烧录 (包含分区表和固件)
idf.py -p COM3 flash

# 5. 监视日志 (检查PSRAM初始化)
idf.py monitor
```

### 烧录后验证PSRAM
在串口监视器中应看到:
```
I (1234) spiram: SPI RAM mode: octal, speed: 80MHz
I (1234) spiram: Found 8MB SPI RAM
I (1234) spiram: Adding 8388608 bytes of SPI RAM to heap
```

如果没看到PSRAM初始化日志，检查:
1. 芯片型号是否正确 (ESP32-S3-WROOM-1-N8R8)
2. 硬件连接是否正常
3. `idf.py fullclean` 是否执行

---

## 预期效果

### 内存对比

| 指标 | 修复前 | 启用PSRAM后 (预期) |
|------|--------|-------------------|
| 内部SRAM剩余 | ~6.7KB | ~100-200KB |
| **PSRAM可用** | 0 | **~8MB** |
| **总可用内存** | ~6.7KB | **~8.1MB** |
| Web服务 | 失败 | ✅ 正常启动 |
| 串口CLI | 失败 | ✅ 正常启动 |
| PC软件按钮 | 无响应 | ✅ 正常响应 |

### PSRAM启用后的日志预期
```
I (1234) spiram: SPI RAM mode: octal, speed: 80MHz
I (1234) spiram: Found 8MB SPI RAM
I (1234) spiram: Adding 8388608 bytes of SPI RAM to heap
...
I (xxxxx) app_main: Heap before web start: free=8240000, min=8230000
I (xxxxx) app_main: Web service started successfully
I (xxxxx) serial_cli: UART line CLI task started (stack=4KB)
```

---

## 验证方法

### 1. 检查PSRAM初始化
```
I (1234) spiram: SPI RAM mode: octal, speed: 80MHz
I (1234) spiram: Found 8MB SPI RAM
I (1234) spiram: Adding 8388608 bytes of SPI RAM to heap
```

### 2. 检查总内存
```
I (xxxxx) app_main: Heap before web start: free=8240000, min=8230000
```
预期 `free` > 8000000 (8MB+)

### 3. 检查内存分布 (代码中添加)
```c
ESP_LOGI(TAG, "=== Memory Status ===");
ESP_LOGI(TAG, "Internal RAM: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
ESP_LOGI(TAG, "PSRAM: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
ESP_LOGI(TAG, "Total: %u", (unsigned)esp_get_free_heap_size());
```

预期输出:
```
I (xxxxx) app_main: === Memory Status ===
I (xxxxx) app_main: Internal RAM: 150000      (~150KB内部SRAM)
I (xxxxx) app_main: PSRAM: 8240000            (~8MB PSRAM)
I (xxxxx) app_main: Total: 8390000            (~8.4MB 总计)
```

### 4. 检查服务启动
```
I (xxxxx) app_main: Web service started successfully
I (xxxxx) serial_cli: UART line CLI task started (stack=4KB)
```

### 5. PC软件测试
- 点击"总览"→"刷新" → 应显示设备信息
- 点击"重启" → 设备应重启
- 所有按钮应有正常响应

---

## 如果仍有问题

### 检查最小内存
如果 `min` 值仍然很低 (<10KB)，检查：

1. **PPPoS是否启用** - 消耗大量内存
2. **以太网W5500** - 检查是否检测到有芯片（会占用netif资源）
3. **USB Modem** - 4G模块会占用PPP/netif资源

### 极端内存优化
如果仍不够，可以进一步：

```bash
# 进一步减少LWIP
CONFIG_LWIP_MAX_SOCKETS=8
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=1460

# 禁用部分功能
CONFIG_ROUTER_PPP_ENABLE=n
CONFIG_SYSTEM_USB_CAT1_DETECT=n
```

### PSRAM启用 ✅ 已配置
你的ESP32-S3已外挂8MB PSRAM，已添加配置：

```bash
CONFIG_SPIRAM=y                          # 启用PSRAM
CONFIG_SPIRAM_MODE_OCT=y                 # 八线模式(80M)
CONFIG_SPIRAM_SPEED_80M=y                # 80MHz时钟
CONFIG_SPIRAM_USE_MALLOC=y               # malloc可使用PSRAM
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y   # WiFi/LWIP使用PSRAM
```

**效果**: 可用内存从~6KB扩展到 **8MB+**

### 在代码中使用PSRAM
```c
// 方法1: 自动分配（已启用CONFIG_SPIRAM_USE_MALLOC）
// malloc/calloc会自动优先使用PSRAM
void* buf = malloc(1024 * 1024);  // 1MB直接分配到PSRAM

// 方法2: 显式分配
void* psram_buf = heap_caps_malloc(1024 * 1024, MALLOC_CAP_SPIRAM);

// 方法3: 检查内存分布
ESP_LOGI(TAG, "Internal RAM: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
ESP_LOGI(TAG, "PSRAM: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

### 更换芯片
ESP32-S3 有 512KB SRAM。如仍不足，考虑：
- **ESP32-S3-WROOM-1U-N8R8** (8MB PSRAM, ✅ 你已使用)
- **ESP32-P4** (更大内部SRAM)

---

## 内存监控命令

```bash
# 运行时查看内存
idf.py monitor
# 然后输入: heap

# 或在代码中添加
ESP_LOGI(TAG, "Free heap: %u", (unsigned)esp_get_free_heap_size());
```
