# POST Body数据完整性调试指南

## 问题现状

所有POST请求都返回 "invalid json" 错误，日志显示接收到的body缺少最后一个字符 `}`。

## 新的调试方法

已添加**十六进制日志打印**，可以查看每个字节的实际值。

### 固件端修改

1. **serial_cli.c** - 打印接收到的原始body数据（十六进制）
   - 前64字节的hex dump
   - 最后16字节的hex dump（如果body>64字节）

2. **web_service.c** - 打印接收到的body数据（十六进制）
   - 前128字节的hex dump

### PC软件端修改

- 增加header发送后延迟：50ms
- 增加body发送后延迟：20ms
- 确保数据完全到达设备端

## 测试步骤

### 1. 重新编译固件

```bash
idf.py build flash monitor
```

### 2. 运行PC软件并保存AP配置

### 3. 查看固件日志

你应该看到类似这样的输出：

```
I serial_cli: Successfully read POST body[121]
I serial_cli: 7b 22 77 69 66 69 5f 65  6e 61 62 6c 65 64 22 3a  |{"wifi_enabled":|
I serial_cli: 20 74 72 75 65 2c 20 22  73 73 69 64 22 3a 20 22  | true, "ssid": "|
...
I serial_cli: Last 16 bytes:
I serial_cli: 73 65 64 3a 20 66 61 6c  73 65 7d 00 00 00 00     |sed:false}....|

I web_service: wifi_ap_post: received body[121]
I web_service: 7b 22 77 69 66 69 5f 65  6e 61 62 6c 65 64 22 3a  |{"wifi_enabled":|
...
```

### 4. 分析十六进制数据

重点关注：

1. **最后一个字节**：应该是 `0x7d`（即 `}` 的ASCII码）
2. **是否有意外字节**：如 `0x00`（NULL）、`0x0a`（换行）、`0x0d`（回车）
3. **body长度**：是否真的是121字节

### 5. 可能的情况及解决方案

#### 情况A：最后字节确实是 `0x7d` (`}`)

**说明**：数据完整，问题在cJSON解析器

**解决**：
- 检查是否有不可见字符（如BOM标记 `0xEF 0xBB 0xBF`）
- 检查JSON格式是否符合cJSON要求

#### 情况B：最后字节不是 `0x7d`

**说明**：数据在传输过程中丢失

**可能原因**：
1. 串口传输丢字节
2. UART缓冲区溢出
3. 时序问题

**解决**：
- 进一步增加延迟时间
- 检查UART配置（缓冲区大小、波特率）
- 降低波特率测试（如9600）

#### 情况C：数据中有额外的 `0x00` 或 `0x0a`

**说明**：数据中混入了额外字符

**解决**：
- 检查PC软件发送逻辑
- 检查串口编码设置

## 预期结果

### 成功的日志应该显示

```
I serial_cli: Successfully read POST body[121]
I serial_cli: 7b 22 77 69 66 69 5f 65  6e 61 62 6c 65 64 22 3a  |{"wifi_enabled":|
... (中间数据) ...
I serial_cli: Last 16 bytes:
I serial_cli: 64 3a 20 66 61 6c 73 65  7d                        |d: false}|

I web_service: wifi_ap_post: received body[121]
I web_service: 7b 22 77 69 66 69 5f 65  6e 61 62 6c 65 64 22 3a  |{"wifi_enabled":|
... (中间数据) ...

I web_service: [WIFI] SoftAP config updated
[PCAPI ←] HTTP 200
```

### JSON的十六进制对照

```json
{"wifi_enabled": true, "ssid": "ESP_D7B119", "encryption_mode": "WPA2-PSK", "password": "12345678", "hidden_ssid": false}
```

对应的十六进制（最后20字节）：
```
64 64 65 6e 5f 73 73 69  64 22 3a 20 66 61 6c 73  |dden_sid": fals|
65 7d                                             |e}|
```

- `0x65` = 'e'
- `0x7d` = '}' ← **这个字符必须存在！**

## 如果问题仍然存在

请提供以下信息：

1. **完整的固件日志**（从发送PCAPI命令到收到响应）
2. **serial_cli的hex dump输出**
3. **web_service的hex dump输出**
4. **使用的波特率**
5. **USB转串口芯片型号**（CH340、CP2102、FT232等）

## 临时解决方案

如果问题持续存在，可以考虑：

1. **通过网页配置**：浏览器访问设备IP进行配置
2. **通过AT指令**：使用串口AT命令查询状态
3. **降低波特率**：尝试9600或19200波特率测试

## 修改文件清单

- `PCSoftware/nic_ble_pc/device_serial.py` - 增加发送延迟
- `components/serial_cli/serial_cli.c` - 添加hex dump日志
- `components/webService/src/web_service.c` - 添加hex dump日志

## 更新日期

2026-04-16
