# AP配置JSON解析错误修复

## 问题根因

通过分析固件日志，发现接收到的JSON数据**缺少最后一个字符 `}`**：

```
实际接收: '{"wifi_enabled": true, "ssid": "ESP_D7B119", "encryption_mode": "WPA2-PSK", "password": "12345678", "hidden_ssid": false'
期望接收: '{"wifi_enabled": true, "ssid": "ESP_D7B119", "encryption_mode": "WPA2-PSK", "password": "12345678", "hidden_ssid": false}'
```

**原因**：PC软件在发送PCAPI POST请求时，header行和body数据之间没有适当的延迟和flush，导致：
1. header和body数据在串口缓冲区中可能发生重叠
2. 固件端可能在header还未完全处理时就开始读取body
3. 最后一个字节可能因为时序问题被丢弃

## 修复方案

### 1. PC软件端修复 (`PCSoftware/nic_ble_pc/device_serial.py`)

**修改位置**：第205-220行

**修改内容**：
```python
# 修改前：
self._ser.write(line.encode("utf-8"))
if method == "POST" and body and len(body) > 0:
    self._ser.write(body)
self._ser.flush()

# 修改后：
# 先发送PCAPI header行
self._ser.write(line.encode("utf-8"))
self._ser.flush()  # 确保header完全发送
# 短暂延迟，让固件端有时间处理header并准备接收body
if method == "POST" and body and len(body) > 0:
    import time
    time.sleep(0.01)  # 10ms延迟，确保固件端准备好
    self._ser.write(body)
    self._ser.flush()  # 确保body完全发送
```

**改进点**：
- header发送后立即flush，确保数据完全送出
- 发送body前增加10ms延迟，给固件端处理header的时间
- body发送后再次flush，确保完整传输

### 2. 固件端修复 (`components/serial_cli/serial_cli.c`)

**修改位置**：第284-298行

**修改内容**：
```c
// 修改前：
if (body_len > 0) {
    body = (char *)malloc(body_len + 1u);
    if (!body) {
        pcapi_reply_err(500, "{\"status\":\"error\",\"message\":\"oom\"}");
        return;
    }
    if (read_uart_exact((uint8_t *)body, body_len, PCAPI_BODY_READ_MS) != ESP_OK) {
        free(body);
        pcapi_reply_err(408, "{\"status\":\"error\",\"message\":\"post body read timeout\"}");
        return;
    }
    body[body_len] = '\0';
}

// 修改后：
if (body_len > 0) {
    body = (char *)malloc(body_len + 1u);
    if (!body) {
        pcapi_reply_err(500, "{\"status\":\"error\",\"message\":\"oom\"}");
        return;
    }
    /* Small delay to ensure sender has finished transmitting and all bytes are in UART buffer */
    vTaskDelay(pdMS_TO_TICKS(10));
    if (read_uart_exact((uint8_t *)body, body_len, PCAPI_BODY_READ_MS) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read POST body: expected %u bytes", (unsigned)body_len);
        free(body);
        pcapi_reply_err(408, "{\"status\":\"error\",\"message\":\"post body read timeout\"}");
        return;
    }
    body[body_len] = '\0';
    ESP_LOGI(TAG, "Successfully read POST body[%u]='%.*s'", (unsigned)body_len, (int)body_len, body);
}
```

**改进点**：
- 读取body前增加10ms延迟，确保PC端数据已完全到达串口缓冲区
- 添加成功读取的日志，便于调试
- 添加失败时的详细错误日志

### 3. Web服务增强日志 (`components/webService/src/web_service.c`)

**修改位置**：第1796-1810行

**改进点**：
- 打印接收到的完整body内容
- 打印JSON解析错误的具体位置
- 便于定位JSON格式问题

## 测试步骤

1. **重新编译固件**：
   ```bash
   idf.py build flash monitor
   ```

2. **运行PC软件**：
   - 连接串口
   - 进入网络配置页面
   - 修改AP配置（SSID、密码等）
   - 点击"保存"

3. **验证结果**：
   - 应该看到成功提示
   - 固件日志应该显示：
     ```
     I serial_cli: Successfully read POST body[121]='{"wifi_enabled": true, ...}'
     I web_service: wifi_ap_post: received body[121]='{"wifi_enabled": true, ...}'
     I web_service: [WIFI] SoftAP config updated
     ```

4. **如果仍然失败**：
   - 检查固件日志中的详细信息
   - 确认body长度是否正确
   - 确认JSON内容是否完整

## 技术说明

### 为什么需要延迟？

串口通信是**异步串行通信**，数据以字节流形式传输：

1. PC发送header行：`PCAPI POST /api/wifi/ap 121\r\n`
2. 数据通过USB转串口芯片传输到ESP32
3. ESP32的UART外设接收数据到FIFO缓冲区
4. ESP32的任务从缓冲区读取数据

如果没有延迟：
- PC可能立即发送body数据
- 但ESP32可能还在处理header行（解析路径、长度等）
- 导致body的第一个字节被当作header的一部分处理
- 最终导致body少了一个字节

### 为什么是10ms？

- 121字节 @ 115200 bps ≈ 10ms传输时间
- 给ESP32足够时间处理header行
- 确保所有body字节都已到达UART缓冲区
- 这是一个保守的值，可以根据实际情况调整

## 后续优化建议

1. **添加重试机制**：
   - PC软件在收到400错误时自动重试1-2次
   - 提高在噪声环境下的可靠性

2. **添加校验和**：
   - 在body后添加CRC32校验
   - 固件端验证数据完整性
   - 如校验失败则请求重传

3. **使用ACK/NACK协议**：
   - 固件端收到header后发送ACK
   - PC端收到ACK后再发送body
   - 彻底解决同步问题

4. **增大UART缓冲区**：
   - 在 `uart_driver_install` 时增加RX缓冲区大小
   - 减少数据丢失的可能性

## 相关文件

- `PCSoftware/nic_ble_pc/device_serial.py` - PC软件串口通信
- `components/serial_cli/serial_cli.c` - 固件PCAPI协议处理
- `components/webService/src/web_service.c` - Web服务API处理

## 修改日期

2026-04-16
