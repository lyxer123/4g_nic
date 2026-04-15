# 路由器 AT 指令（4g_nic 固件）

本文档描述 **本仓库路由器固件** 对外暴露的 **类 ESP-AT 文本指令**，用于外部 MCU 通过 UART 做链路测试、版本查询与后续扩展。**不等同于乐鑫官方 ESP-AT 固件**，语义与命令集以本文为准；官方《基础 AT 命令集》等仅作命名参考（见根目录 [`at指令说明.md`](at指令说明.md)）。

实现代码：`components/router_at/`。说明类文档：[`at指令实现.md`](at指令实现.md)。

---

## 1. 设计目标

| 目标 | 说明 |
|------|------|
| 与现有产品一致 | 数据面仍为 iot_bridge + NAT；AT 只做 **控制/查询** 与后续 **socket 类扩展**，不照搬官方全量指令。 |
| 开发 / 量产可切换 | **开发**：默认 **与控制台共用 UART0**（与 `serial_cli`、日志同口，仅 **`AT` 前缀行** 走 AT）。**量产**：可改为 **UART1/UART2 专用**，与调试口分离。 |
| 可配置 + 可写入 NVS | 编译期 **menuconfig** 选择端口；可选 **NVS** 覆盖端口（上电生效，见 §3）。 |

---

## 2. 物理串口与行为

### 2.1 编译选项（`idf.py menuconfig` → **Router AT (MCU)**）

| 配置项 | 含义 |
|--------|------|
| `CONFIG_ROUTER_AT_ENABLE` | 是否启用路由器 AT 子集。 |
| `CONFIG_ROUTER_AT_UART_NUM` | AT 会话使用的 UART 端口号 **0 / 1 / 2**。 |
| `CONFIG_ROUTER_AT_UART_BAUD` | **专用 UART**（与控制台不是同一端口时）的波特率，默认 115200。 |
| `CONFIG_ROUTER_AT_UART_TX_GPIO` / `RX_GPIO` | **仅当 AT 端口 ≠ 控制台 UART** 时用于 `uart_set_pin`；请按原理图修改。`-1` 表示不在本组件内绑脚（需自行保证引脚）。 |

### 2.2 与控制台共用 UART（典型：开发阶段 UART0）

- 控制台 UART 由 `CONFIG_ESP_CONSOLE_UART_NUM` 决定（工程内常与 **UART0** 一致）。
- 当 **`CONFIG_ROUTER_AT_UART_NUM` 等于 `CONFIG_ESP_CONSOLE_UART_NUM`**（并经过 §3 的 NVS 解析之后仍相等）时，AT 与 **`serial_cli` / PCAPI** 共用 RX：
  - 一行若以 **`AT`** / **`AT+`** / **`ATE`** 等形式识别为 AT 行（见实现），则 **只走 AT**，不再当作 `help`、`PCAPI` 等。
  - 其它行逻辑 **不变**（`PCAPI`、`GET /…`、`help` 等）。
- **注意**：`ESP_LOG` 仍可能向同一 UART 打印，MCU 解析时需容忍日志穿插，或降低日志等级 / 使用专用 UART 规避。

### 2.3 专用 UART（典型：量产 UART1 或 UART2）

- 当 **有效 AT 端口 ≠ 控制台 UART** 时，组件会 **单独 `uart_driver_install`** 并在独立任务中读行，该口 **只接受 AT**（非 AT 行返回 `ERROR`）。
- 需在 menuconfig 中配置 **TX/RX GPIO**（或与硬件默认一致后 `-1`，依板级而定）。

---

## 3. NVS 覆盖端口（可选，便于 PC 工具 / 出厂写入）

| 项 | 值 |
|----|-----|
| 命名空间 | `router_at` |
| 键 | `uart_num` |
| 取值 | `0`～`2` |

若该键存在且合法，则 **覆盖** `CONFIG_ROUTER_AT_UART_NUM`，用于在不改固件的情况下切换 AT 物理口。**修改后需重启生效**。

应用侧 API：`router_at_set_uart_num_nvs(uint8_t)`（`components/router_at/include/router_at.h`）。PC 上位机可在后续通过 HTTP/串口命令封装写入。

---

## 4. 响应格式约定

- 成功结尾：`\r\nOK\r\n`
- 失败结尾：`\r\nERROR\r\n`
- 多行信息：在 `OK` 前输出正文行（每行 `\r\n` 结尾），风格接近常见 AT 模组。

**回显 `ATE`**

- `ATE0`：关闭回显（默认 **关闭**，便于 MCU 脚本）。
- `ATE1`：开启后，在结果前 **回显整行命令**（简化实现：按行回显，非官方逐字符回显）。

---

## 5. 已实现的「基础类」指令

与官方《基础 AT 命令集》名称相近者优先列出；**未列出的官方指令均未实现**。

| 指令 | 说明 |
|------|------|
| `AT` | 链路就绪测试，返回 `OK`。 |
| `ATE0` | 关闭命令回显。 |
| `ATE1` | 开启命令回显（见 §4）。 |
| `AT+GMR` | 查询版本：应用版本（`esp_app_desc`）与 **IDF 版本**（`esp_get_idf_version()`）。 |
| `AT+RST` | 返回 `OK` 后 **重启设备**（与官方「复位模块」语义类似，请谨慎使用）。 |
| `AT+CMD` | 文本列出当前固件支持的命令名（简化版 `+CMD:` 行，非官方 Test 查询语法）。 |
| `AT+PING` | 链路就绪/存活检查，返回 `+PING:ok cmd=ping device=4g_nic`。 |
| `AT+MODE` | 查询当前 `work_mode_id`；配合 `AT+MODE=<id>` 设置工作模式。 |
| `AT+MODEMINFO` | 查询 4G 调制解调器信息（SIM/IMEI/IMSI/运营商/RSSI 等），等价于 CLI `modem_info`。 |
| `AT+W5500` | 查询 W5500 是否存在及探测到的版本号，等价于硬件检测结果。 |
| `AT+W5500IP` | 查询 W5500 对应以太网接口的 IP、掩码和网关；如果当前以太接口无 IP，则返回 `0.0.0.0`。 |
| `AT+USB4G` | 查询 USB Cat1 4G 调制解调器是否存在，返回 `present` 及 VID/PID。 |
| `AT+USB4GIP` | 查询 USB Cat1 PPP 接口的 IP、掩码和网关；若未分配 IP 则返回 `0.0.0.0`。 |
| `AT+NETCHECK` | 检测路由器外网连通性，通过访问 `http://www.baidu.com/` 判断是否能够联网。 |
| `AT+SYSRAM` | 查询内部堆与 DMA 能力堆空闲（`heap_caps`），用于诊断。 |
| `AT+ROUTER` | **本项目扩展**：返回当前 **有效 UART 号**、是否与控制台 **共用**、**当前波特率** 等。 |

---

## 6. 与 `serial_cli` 同时使用时（共用 UART0）

1. 调试人机命令：照常输入 `help`、`PCAPI …` 等。  
2. MCU 发 AT：行首为 **`AT`** 规范前缀，例如 `AT`、`AT+GMR`。  
3. 勿在 AT 行前加会与 PCAPI 冲突的前缀；需要 **`GET /api/...`** 的 PCAPI 行保持原样（不以 `AT` 开头）。

---

## 7. 后续计划（文档与代码同步迭代）

- TCP/IP 子集（如类 `AT+CIPSTART` / `+IPD`）与路由/NAT 协同。  
- MQTT/HTTP 等按 [`at指令实现.md`](at指令实现.md) 分期。  
- 上位机通过 Web/串口写入 `router_at` NVS 或专用配置项，实现「一键切 UART」。

---

## 8. 变更记录

| 日期 | 说明 |
|------|------|
| 2026-04-12 | 首版：UART 可配置 + NVS 覆盖、`AT`/`ATE`/`AT+GMR`/`AT+RST`/`AT+CMD`/`AT+SYSRAM`/`AT+ROUTER`；与 `serial_cli` 共用 UART0 时分流。 |
| 2026-04-15 | 新增 AT+PING、AT+MODE 查询/设置命令；将现有 CLI `ping`、`mode_get`/`mode_set` 语义映射为 AT 子集。 |
| 2026-04-15 | 新增 AT+MODEMINFO 命令，映射当前 CLI `modem_info`。 |
| 2026-04-15 | 新增 AT+W5500 命令，查询 W5500 硬件是否存在以及版本寄存器值。 |
| 2026-04-15 | 新增 AT+W5500IP 命令，查询 W5500 以太网接口的 IP、掩码和网关。 |
| 2026-04-15 | 新增 AT+USB4G 命令，检测 USB Cat1 4G 调制解调器存在性并返回 VID/PID。 |
| 2026-04-15 | 新增 AT+USB4GIP 命令，查询 USB Cat1 PPP 接口的 IP、掩码和网关。 |
| 2026-04-15 | 新增 AT+NETCHECK 命令，检测外网连通性（访问 www.baidu.com）。 |
