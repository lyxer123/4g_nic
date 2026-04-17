# AP配置保存错误排查与解决方案

## 问题描述

在PC软件中保存AP配置时，出现以下错误：

```
[PCAPI →] PCAPI POST /api/wifi/ap 121
[PCAPI →] POST 载荷 121 字节: {"wifi_enabled": true, "ssid": "ESP_D7B119", "encryption_mode": "WPA2-PSK", "password": "12345678", "hidden_ssid": false}
[PCAPI ←] HTTP 400 · 43 字节
[PCAPI ←] {"status":"error","message":"invalid json"}
```

## 问题分析

### 已完成的修复

1. **固件端添加了详细的JSON解析错误日志** (`components/webService/src/web_service.c`)
   - 添加了接收到的body内容打印
   - 添加了cJSON解析错误位置打印
   - 添加了原始body长度打印

### 可能的原因

1. **串口传输过程中数据损坏**
   - PCAPI协议通过串口传输JSON数据
   - 如果串口通信不稳定，可能导致字节丢失
   - 特别是大数据包更容易出现问题

2. **编码问题**
   - Python使用UTF-8编码
   - 固件端也应该使用UTF-8解析
   - 但目前看来编码应该不是问题

3. **cJSON解析器限制**
   - cJSON对JSON格式要求严格
   - 不支持某些特殊的JSON扩展

## 解决方案

### 方案1：重新编译固件并查看详细日志（推荐）

1. 重新编译固件（已添加详细日志）：
   ```bash
   idf.py build flash
   ```

2. 运行PC软件并重现问题

3. 查看固件端日志，应该会看到类似：
   ```
   I web_service: wifi_ap_post: received body[121]='{"wifi_enabled": true, ...}'
   E web_service: wifi_ap_post: JSON parse failed at: <错误位置>
   E web_service: wifi_ap_post: raw body len=121
   ```

4. 根据日志确定具体问题

### 方案2：测试串口通信稳定性

1. 使用串口调试工具直接发送测试命令：
   ```
   PCAPI POST /api/wifi/ap 48
   {"ssid":"TestAP","encryption_mode":"WPA2-PSK","password":"12345678"}
   ```

2. 观察是否仍然出现JSON解析错误

3. 如果直接发送也失败，说明是固件端问题
4. 如果直接发送成功，说明是PC软件传输问题

### 方案3：临时绕过方案

如果问题持续存在，可以暂时使用以下方法：

1. **通过网页配置AP**：
   - 浏览器访问设备IP
   - 在网络配置页面设置AP参数
   - 保存配置

2. **通过AT指令查询AP状态**：
   ```
   AT+ROUTER
   ```

## 代码修改说明

### 固件端修改

文件：`components/webService/src/web_service.c`

修改内容：
- 在 `uri_wifi_ap_post` 函数中添加了详细的调试日志
- 在JSON解析失败时打印错误位置和原始数据

### 下次更新建议

如果确认是串口传输问题，可以考虑：

1. **增加重试机制**：PC软件在收到400错误时自动重试
2. **添加校验和**：在PCAPI协议中添加数据完整性校验
3. **优化传输方式**：对大数据包进行分片传输

## 验证步骤

1. 重新编译并烧录固件
2. 打开串口监视器
3. 运行PC软件
4. 尝试保存AP配置
5. 查看固件日志中的详细信息
6. 根据日志进一步分析问题

## 联系信息

如果问题仍然存在，请提供以下信息：
- 固件日志（包含wifi_ap_post相关行）
- PC软件日志
- 使用的ESP32-S3型号
- 串口波特率设置
