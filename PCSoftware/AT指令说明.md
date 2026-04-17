# 4G NIC 路由器 AT 指令说明

本文档说明 ESP32-S3 路由器固件中实现的 AT 指令集，可通过串口发送这些指令来查询和控制设备。

## 使用方式

- 通过串口发送 AT 指令（与 serial_cli 共用 UART 时使用专用线路）
- 以 `AT` 开头的行会被 router_at 模块处理
- 其他命令由 serial_cli 处理（如 `help`、`modem_info` 等）
- 所有响应都会在串口日志中显示

## 基础指令

| 指令 | 类型 | 说明 | 示例响应 |
|------|------|------|----------|
| `AT` | 执行 | 测试 AT 是否就绪 | `OK` |
| `ATE0` | 执行 | 关闭命令回显 | `OK` |
| `ATE1` | 执行 | 开启命令回显 | `OK` |
| `AT+GMR` | 查询 | 查询固件版本信息 | `AT version:xxx`<br>`SDK:xxx` |
| `AT+IDF` | 查询 | 查询 ESP-IDF 版本 | `+IDF:v5.x.x` |
| `AT+CHIP` | 查询 | 查询芯片信息 | `+CHIP:chip=ESP32-S3,cores=2,rev=x,features=...` |
| `AT+MEM` | 查询 | 查询 Flash 和 PSRAM 大小 | `+MEM:flash_size=xxx,psram_size=xxx,psram_present=x` |
| `AT+RST` | 执行 | 重启设备 | `OK` (然后重启) |
| `AT+CMD` | 查询 | 列出所有支持的 AT 指令 | `+CMD:AT,ATE0,ATE1,...` |
| `AT+SYSRAM` | 查询 | 查询系统 RAM 使用情况 | `+SYSRAM:internal_free=xxx,...` |

## 时间管理指令

| 指令 | 类型 | 说明 | 示例 |
|------|------|------|------|
| `AT+TIME` | 查询 | 查询当前系统时间 | `+TIME:local_time=2024-01-01 12:00:00` |
| `AT+TIME=YYYY-MM-DD HH:MM:SS` | 设置 | 设置系统时间 | `AT+TIME="2024-01-01 12:00:00"` |

## 路由器相关指令

| 指令 | 类型 | 说明 | 示例响应 |
|------|------|------|----------|
| `AT+ROUTER` | 查询 | 查询路由器 UART 配置信息 | `+ROUTER:uart=x,shared=x,baud=115200,...` |
| `AT+MODE?` | 查询 | 查询当前工作模式 | `+MODE?:work_mode_id=x` |
| `AT+MODESET=<id>` | 设置 | 设置工作模式（**新**） | `AT+MODESET=0` → 显示配置提示 |
| `AT+PING` | 执行 | Ping 测试（设备级） | `+PING:ok cmd=ping device=4g_nic` |

## 4G 模组相关指令

> 注意：这些指令仅在启用 `CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM` 配置时可用

| 指令 | 类型 | 说明 | 示例响应 |
|------|------|------|----------|
| `AT+MODEMINFO` | 查询 | 查询 4G 模组详细信息 | `+MODEMINFO:ppp_has_ip=1,iccid=...,imsi=...,imei=...,operator=...,network_mode=...,act=x,rssi=x,ber=x,...` |
| `AT+MODEMTIME` | 查询 | 查询 4G 模组网络时间 | `+MODEMTIME:时间字符串` |

### MODEMINFO 字段说明

- `ppp_has_ip`: PPP 连接是否已获取 IP（1=是，0=否）
- `iccid`: SIM 卡 ICCID
- `imsi`: SIM 卡 IMSI
- `imei`: 模组 IMEI
- `operator`: 运营商名称
- `network_mode`: 网络模式（如 LTE, GSM 等）
- `act`: 接入技术类型
- `rssi`: 信号强度指示
- `ber`: 误码率
- `manufacturer`: 模组制造商
- `model`: 模组型号
- `fw_version`: 模组固件版本

## W5500 以太网相关指令

| 指令 | 类型 | 说明 | 示例响应 |
|------|------|------|----------|
| `AT+W5500` | 查询 | 检测 W5500 芯片是否存在 | `+W5500:present=1,version=0x02` |
| `AT+W5500IP` | 查询 | 查询 W5500 以太网 IP 信息 | `+W5500IP:ip=192.168.1.100,mask=255.255.255.0,gateway=192.168.1.1` |

## USB 4G 相关指令

| 指令 | 类型 | 说明 | 示例响应 |
|------|------|------|----------|
| `AT+USB4G` | 查询 | 检测 USB 4G 模组 | `+USB4G:present=1,vid=0x1234,pid=0x5678` |
| `AT+USB4GIP` | 查询 | 查询 USB 4G PPP 接口 IP | `+USB4GIP:ip=10.0.0.1,mask=255.255.255.252,gateway=10.0.0.2` |

## 网络检测指令

| 指令 | 类型 | 说明 | 示例响应 |
|------|------|------|----------|
| `AT+NETCHECK` | 执行 | 检测网络连通性（访问百度） | `+NETCHECK:ok,method=http,target=www.baidu.com,status=200` |

## serial_cli 命令（非 AT 指令）

这些命令由 serial_cli 模块处理，不以 AT 开头：

| 命令 | 说明 |
|------|------|
| `help` | 列出所有可用的 serial_cli 命令 |
| `modem_info` | 查询 4G 模组详细信息（类似 AT+MODEMINFO） |

## 响应格式说明

- 所有成功响应以 `OK` 结尾
- 所有失败响应以 `ERROR` 结尾
- 查询类指令会先返回数据行，再返回 `OK`
- 设置类指令成功后返回 `OK`，失败返回 `ERROR`

## 注意事项

1. **UART 共享**：当 AT 与日志控制台共用同一 UART 时，只有以 `AT` 开头的完整行才会被 router_at 处理
2. **命令回显**：默认关闭回显，可使用 `ATE1` 开启，`ATE0` 关闭
3. **重启指令**：`AT+RST` 会立即重启设备，请谨慎使用
4. **4G 模组指令**：`AT+MODEMINFO` 和 `AT+MODEMTIME` 需要设备连接了 4G 模组并启用相应配置
5. **时间设置**：`AT+TIME=` 设置时间时需要用引号包围时间字符串

## 版本信息

- 固件版本：查看 `AT+GMR` 输出
- ESP-IDF 版本：查看 `AT+IDF` 输出
- 芯片信息：查看 `AT+CHIP` 输出

## 故障排查

| 问题 | 可能原因 | 解决方法 |
|------|----------|----------|
| 返回 `ERROR` | 指令不存在或参数错误 | 使用 `AT+CMD` 查看支持的指令列表 |
| 无响应 | 串口未正确连接 | 检查串口连接和波特率设置 |
| `AT+MODEMINFO` 返回 `ERROR` | 未连接 4G 模组 | 检查硬件连接和固件配置 |
| `AT+W5500` 显示 `present=0` | W5500 未连接 | 检查 W5500 硬件连接 |
| `AT+USB4G` 显示 `present=0` | USB 4G 模组未插入 | 插入 USB 4G 模组并等待识别 |

## 相关文档

- [AT 指令实现评估](../at指令实现.md) - AT 指令架构设计和实现规划
- [AT 指令说明（乐鑫 ESP-AT 参考）](../at指令说明.md) - 乐鑫官方 ESP-AT 指令集参考
