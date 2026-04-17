# 在本路由器中嵌入「类 ESP-AT」能力：评估与实现思路

本文档面向 **4G NIC / 路由器工程**（ESP-IDF + esp-iot-bridge，多接口 NAT，已有 4G / Wi‑Fi / W5500 等），评估 **外部 MCU 通过类 AT 指令使用网络** 的可行性、难度与推荐路线，供架构决策与分期开发参考。

---

## 1. 目标澄清：何为「类似 ESP-AT」

### 1.1 官方 ESP-AT 的典型能力

乐鑫 **ESP-AT** 固件的核心价值是：在 **一颗芯片上** 把 **Wi‑Fi / 蓝牙协议栈 + 网络栈** 封装成 **串口 AT 命令**，上位机（MCU）只需发 `AT+` 指令即可完成：

- 配网、连接 AP、建立 TCP/UDP/SSL 连接、MQTT 等；
- 无需在 MCU 上实现 802.11、TCP/IP、TLS 等。

其本质是 **「网络协处理器」** 协议，命令集大、行为与模组固件版本强绑定。

### 1.2 与本工程的关系

本仓库 **不是** ESP-AT 固件，而是 **路由器 + NAT + 多 WAN 接入**：

- 数据面已由 **iot_bridge + lwIP** 完成，**上网通道** 已由 4G PPP / STA / 有线等 **工作模式** 选路与维护；
- 已有 **管理面**：**串口 `serial_cli`（`help`、PCAPI）**、**BLE JSON**、**HTTP REST**。

因此，「类似 ESP-AT」在本项目里更合理的含义是：

> **外部 MCU 通过 UART（或第二路串口）使用文本行协议，完成「发数据、建连、上网」等能力，而不必在 MCU 上跑完整 TCP/IP 栈或关心当前是 4G 还是有线。**

这与 **官方 ESP-AT 全量兼容** 不是同一目标；若强行追求 **命令字、参数、回显与 ESP-AT 逐条一致**，工作量会接近 **移植或重写一套 AT 解释器**，性价比通常较低。

---

## 2. 乐鑫 ESP-AT 官方功能分层与指令表

以下依据乐鑫 **[ESP-AT User Guide（latest，ESP32）](https://docs.espressif.com/projects/esp-at/en/latest/AT_Command_Set/index.html)** 的 **AT Command Set** 结构整理：同一分层内指令为 **代表性列举**（固件裁剪/芯片系列会导致子集差异，以官方页面为准）。

### 2.1 分层模型（逻辑视图）

| 分层 | 作用 | 典型内容 |
|------|------|----------|
| **L0 基础与系统** | 链路测试、复位、版本、UART/睡眠、NVS 与系统参数 | `AT`、`AT+RST`、`AT+GMR`、`AT+UART_*`、`AT+SYS*` 等 |
| **L1 射频与链路介质** | Wi‑Fi / 以太网 PHY 侧：模式、连接 AP、SoftAP、扫描、DHCP、MAC/IP | `AT+CW*`、`AT+CIPSTA` / `AT+CIPAP`、`AT+CIPETH*` 等 |
| **L2 网络与传输（套接字）** | TCP/UDP/SSL 客户端/服务器、多连接、DNS、透传、被动收包 | `AT+CIP*`（除部分与 Wi‑Fi 合名者）、`AT+PING` 等 |
| **L3 应用协议** | HTTP 客户端、MQTT、WebSocket 等 | `AT+HTTP*`、`AT+MQTT*`、`AT+WS*` 等 |
| **L4 配网与其它** | SmartConfig、BluFi、BLE/BT AT 等 | `AT+CWSTARTSMART`、`AT+BLUFI*`、`AT+BLE*` 等 |

官方文档另约定：**命令类型** 可为 Test（`=?`）、Query（`?`）、Set（`=…`）、Execute（无参）；响应以 **`OK` / `ERROR`** 结尾，并有大量 **主动上报**（如 `+IPD`、`WIFI GOT IP` 等）。

### 2.2 L0 — 基础 AT 命令（节选）

| 指令 | 功能摘要 |
|------|----------|
| `AT` | 测试 AT 是否就绪 |
| `AT+RST` | 模组复位 |
| `AT+GMR` | 查询 AT 版本 / SDK / 编译时间等 |
| `AT+CMD` | 列出当前固件支持的命令与类型 |
| `ATE` | 开关命令回显 |
| `AT+RESTORE` | 恢复出厂参数 |
| `AT+UART_CUR` / `AT+UART_DEF` | 当前 / 默认 UART 参数 |
| `AT+SYSRAM` | 查询堆内存 |
| `AT+SYSMSG` / `AT+SYSMSGFILTER*` | 系统提示与过滤 |
| `AT+SYSFLASH` / `AT+SYSMFG` | 用户分区 / 制造 NVS |
| `AT+SYSSTORE` | 是否将部分命令参数写入 NVS |
| `AT+SLEEP` / `AT+GSLP` / `AT+SLEEPWKCFG` | 睡眠与唤醒 |
| `AT+RFPOWER` / `AT+RFCAL` | RF 功率与校准 |
| `AT+SYSTIMESTAMP` / `AT+SYSLOG` 等 | 时间戳、错误码提示等 |

**完整基础命令表（含每条中文说明）** 见根目录 **[`at指令说明.md`](at指令说明.md)**。

### 2.3 L1 — Wi‑Fi AT 命令（节选）

| 指令 | 功能摘要 |
|------|----------|
| `AT+CWINIT` | 初始化/反初始化 Wi‑Fi 驱动 |
| `AT+CWMODE` | Station / SoftAP / 混合模式 |
| `AT+CWSTATE` | 查询 Wi‑Fi 状态与信息 |
| `AT+CWJAP` | 连接 AP（SSID/密码等） |
| `AT+CWRECONNCFG` | 重连策略 |
| `AT+CWLAP` / `AT+CWLAPOPT` | 扫描 AP |
| `AT+CWQAP` | 断开 STA |
| `AT+CWSAP` / `AT+CWLIF` / `AT+CWQIF` | SoftAP 参数、已连 STA、踢 STA |
| `AT+CWDHCP` / `AT+CWDHCPS` | DHCP 与地址池 |
| `AT+CIPSTA` / `AT+CIPAP` | STA / SoftAP 的 IP 配置 |
| `AT+CIPSTAMAC` / `AT+CIPAPMAC` | STA / SoftAP MAC |
| `AT+CWSTARTSMART` / `AT+CWSTOPSMART` | SmartConfig |
| `AT+WPS` / `AT+CWJEAP` / `AT+CWHOSTNAME` 等 | WPS、企业网、主机名等 |

### 2.4 L1 — 以太网 AT（ESP-AT 侧，可选编译）

| 指令 | 功能摘要 |
|------|----------|
| `AT+CIPETHMAC` | 以太网 MAC |
| `AT+CIPETH` | 以太网 IP/网关/掩码 |

### 2.5 L2 — TCP/IP AT 命令（节选）

| 指令 | 功能摘要 |
|------|----------|
| `AT+CIPV6` | IPv6 开关 |
| `AT+CIPSTATE` | 查询连接信息 |
| `AT+CIPDOMAIN` | 域名解析 |
| `AT+CIPSTART` / `AT+CIPSTARTEX` | 建立 TCP/UDP/SSL 连接 |
| `AT+CIPSEND` / `AT+CIPSENDEX` / `AT+CIPSENDL*` | 发送数据（含扩展、长数据） |
| `AT+CIPCLOSE` | 关闭连接 |
| `AT+CIFSR` | 本地 IP/MAC |
| `AT+CIPMUX` | 多连接模式 |
| `AT+CIPSERVER` / `AT+CIPSERVERMAXCONN` | TCP/SSL 服务器 |
| `AT+CIPMODE` | 普通 / 网络透传模式 |
| `AT+CIPSSLC*` | SSL 客户端证书/密码套件/SNI 等 |
| `AT+CIPRECVTYPE` / `AT+CIPRECVDATA` / `AT+CIPRECVLEN` | 主动/被动收包 |
| `AT+CIPDINFO` | `+IPD` 是否带远端 IP |
| `AT+PING` | Ping |
| `AT+CIPDNS` / `AT+MDNS` | DNS、mDNS |
| `AT+CIPSNTPCFG` / `AT+CIPSNTPTIME` / `AT+CIPSNTPINTV` | SNTP 与时间 |
| `AT+CIUPDATE` / `AT+CIPFWVER` | 网络升级相关（依固件） |
| `AT+CIPTCPOPT` | Socket 选项 |

**异步上报（与 L2 强相关）**：如 `+IPD`、`<link_id>,CONNECT` / `CLOSED`、`+LINK_CONN` 等（见官方 *AT Messages*）。

### 2.6 L3 — HTTP / MQTT（节选）

**HTTP**

| 指令 | 功能摘要 |
|------|----------|
| `AT+HTTPCLIENT` | 通用 HTTP 客户端请求 |
| `AT+HTTPGETSIZE` / `AT+HTTPCGET` | 资源大小 / GET |
| `AT+HTTPCPOST` / `AT+HTTPCPUT` | POST/PUT（可配合 `>` 传体） |
| `AT+HTTPURLCFG` | 长 URL 配置 |
| `AT+HTTPCHEAD` / `AT+HTTPCFG` | 请求头 / 客户端配置（含证书策略） |

**MQTT**

| 指令 | 功能摘要 |
|------|----------|
| `AT+MQTTUSERCFG` | 用户配置（含 TLS 方案等） |
| `AT+MQTTLONGCLIENTID` / `USERNAME` / `PASSWORD` | 长客户端 ID/用户名/密码 |
| `AT+MQTTCONNCFG` / `AT+MQTTALPN` / `AT+MQTTSNI` | 连接参数、ALPN、SNI |
| `AT+MQTTCONN` | 连接 Broker |
| `AT+MQTTPUB` / `AT+MQTTPUBRAW` | 发布 |
| `AT+MQTTSUB` / `AT+MQTTUNSUB` | 订阅/退订 |
| `AT+MQTTCLEAN` | 断开并清理 |

（另有 **WebSocket**、**经典蓝牙/BLE** 等独立章节，此处不展开枚举。）

---

## 3. 可模仿性综合判断（针对本 4G NIC 路由器）

本设备角色是 **路由器 + 多 WAN + NAT**，不是 **给 MCU 当 Wi‑Fi 网卡模组**。因此 **L1 里大量「STA 连 AP / SoftAP 配网」类指令与现网架构不对齐**；**最宜借鉴的是 L0 形态 + L2/L3 语义**（套接字与应用协议），而非照搬 Wi‑Fi AT 全集。

### 3.1 分层：建议「模仿 / 化用 / 不建议」

| 分层 | 与本项目关系 | 建议 |
|------|----------------|------|
| **L0 基础** | 需要「探测、版本、可选复位/鉴权」 | **化用**：保留 `AT` 测试、`AT+GMR` 类版本查询；`AT+RST` 慎用（等同整机重启）；UART 参数若专用口可由 Kconfig 固定，不必 AT 在线改 |
| **L1 Wi‑Fi** | 路由器已统一管理 STA/AP/模式 | **不建议照搬**：`AT+CWJAP`、`AT+CWSAP` 等与 **网页/BLE/工作模式** 功能重叠且语义冲突 |
| **L1 以太网 `AT+CIPETH*`** | 本机有 W5500 等，但由 **work_mode / bridge** 管理 | **不建议逐字兼容**：若需查询，可用 **自定义** `AT+NETIF?` 返回摘要，而非 ESP-AT 的 ETH 命令 |
| **L2 TCP/IP** | MCU 只需经 NAT 访问外网 | **优先模仿**：`AT+CIPMUX`、`AT+CIPSTART`、`AT+CIPSEND`、`+IPD`、`AT+CIPCLOSE`、`AT+PING`、`AT+CIPDOMAIN`、`AT+CIPSTATE` 等 **语义**（命令名可同名或加前缀避免纠纷） |
| **L3 HTTP/MQTT** | 物联网常见 | **可选模仿**：`AT+HTTPCLIENT` 或精简版；`AT+MQTTCONN` / `AT+MQTTPUB` / `AT+MQTTSUB` 等 **子集**，TLS 与证书策略成本高，宜分期 |
| **L4 配网** | 已有 Web / BLE | **不重复用 UART AT**：BluFi/SmartConfig 类与现有产品路径重复 |

### 3.2 指令级：对照表（便于产品取舍）

| ESP-AT 指令（类） | 路由器侧「模仿」适宜度 | 说明 |
|-------------------|-------------------------|------|
| `AT` / `AT+GMR` | **高** | 握手与版本，利于 MCU 脚本 |
| `ATE` | **中** | 调试有用；量产可关 |
| `AT+RST` | **低** | 建议映射为「重启路由」或禁用，避免与远程管理预期不符 |
| `AT+UART_*` | **低** | 专用 UART 固定波特率更简单 |
| `AT+CWJAP` / `AT+CWMODE` / `AT+CWLAP` … | **低** | 与路由控制面重复；应由 Web/API/模式管理 |
| `AT+CIPSTART` / `AT+CIPSEND` / `+IPD` / `AT+CIPCLOSE` | **高** | 与「MCU 不跑 IP 栈、只跑业务」目标一致 |
| `AT+CIPMUX` / `AT+CIPMODE`（透传） | **中高** | 多连接与透传为刚需时需实现；注意 RAM 与 UART 互斥 |
| `AT+CIPSSLC*` / SSL 版 `CIPSTART` | **中** | 依赖 mbedTLS/证书与 RAM，宜二期 |
| `AT+PING` / `AT+CIPDOMAIN` | **高** | 排障与 DNS 解析常用 |
| `AT+CIPSNTPCFG` / `AT+CIPSNTPTIME` | **中** | 路由已可有 SNTP；可提供简化查询 |
| `AT+HTTPCLIENT` 等 | **中** | 省 MCU 拼 HTTP；需注意 URL 长度与 TLS |
| `AT+MQTTCONN` 等 | **中** | 与云平台对接省事；长连接保活与 TLS 工作量大 |
| `AT+CIUPDATE`（网络升级模组） | **低** | 固件升级逻辑与本工程 OTA 流程不同，勿混用 |

### 3.3 小结

- **最值得模仿的是 ESP-AT 的「L2 套接字层 + 少量 L0 + 按需 L3」**，与当前 **「上网通道已在路由器侧」** 的设定一致。  
- **最不值得追求的是 L1 Wi‑Fi AT 全兼容**：本机不是 **ESP 作 STA 模组** 的产品形态。  
- 具体命令名是否 **与乐鑫字符串完全一致** 属于 **兼容性/法务/维护** 决策；技术上更稳妥的是 **文档完备的「AT 风格子集」**，并在文档中 **显式对照** 上表与官方 [ESP-AT Command Set](https://docs.espressif.com/projects/esp-at/en/latest/AT_Command_Set/index.html)。

---

## 4. 本工程已有基础（对 MCU 侧的价值）

| 能力 | 说明 |
|------|------|
| **多接入与 NAT** | 4G、Wi‑Fi STA、W5500 等已由 bridge 与模式管理统一进 **数据面**；MCU **无需选择物理出口**，由路由器保证「有可用上行」即可（具体仍受现场拨号、信号、网线等影响）。 |
| **稳定 IP 栈** | 连接建立、重连、DHCP 等已在设备侧完成，MCU侧只需 **会话级** 抽象（如 socket id）。 |
| **现有文本通道** | **UART**：`serial_cli` 行模式 + PCAPI；**BLE**：JSON。可复用「一行一请求」的交互模型。 |

**结论**：「让 MCU 不考虑上网细节」在 **网络基础设施** 层面，本工程 **已经具备**；缺口在于 **给 MCU 的一套专用、易集成的 **应用层协议**（是否采用 AT 语法是产品选择问题）。

---

## 5. 方案对比（从易到难）

### 方案 A：透传 / 固定隧道（最小 AT 面）

- **做法**：MCU 侧仅发少量 AT 用于「进入透传」「绑定远端 IP:端口」，之后 **字节流双向透明**（类似 TCP 透传模式）。
- **优点**：实现量小，协议简单，易在 MCU 上解析。
- **缺点**：灵活性差；多连接、多会话需多隧道或再扩展。

**复杂度**：低。  
**与现有工程**：需在固件侧增加 **任务 + socket**，与 `serial_cli` **UART 资源是否共用** 需单独设计（见第 7 节）。

---

### 方案 B：「AT 风格」的 TCP/UDP 子集（推荐平衡点）

- **做法**：自定义 **与 ESP-AT 神似但不保证兼容** 的命令集，例如（示例）：
  - `AT+NETSTATUS?` → 返回 WAN/LAN 简要状态；
  - `AT+CIPSTART=<id>,<tcp|udp>,<host>,<port>`；
  - `AT+CIPSEND=<id>,<len>` → MCU 再发二进制或十六进制负载；
  - `+IPD,<id>,<len>:...` 异步上报下行数据；
  - `AT+CIPCLOSE=<id>`。
- **底层**：全部映射到 **lwIP `socket()`**，出站走 **系统默认路由**（即当前路由器已选好的上行），**不重复实现 Wi‑Fi/PPP**。

**复杂度**：中。需 **状态机、多连接表、缓冲区大小与流控**（避免 RAM 爆）、**与日志/CLI 共用 UART 时的互斥**。

**实现可能性**：**高**。与官方 ESP-AT 的 Wi‑Fi AT 不同，此处 **不涉及 802.11 控制面**，仅 **socket 封装**，技术路径清晰。

---

### 方案 C：MQTT(S) / HTTP 客户端 AT（垂直场景）

- **做法**：只暴露 **MQTT 发布订阅** 或 **HTTP POST/GET** 的 AT，由固件内置 **esp-mqtt** / **esp_http_client** 完成。

**复杂度**：中偏高（TLS、证书、长连接保活），但 **命令面** 可比「通用 TCP AT」更窄。

**适用**：物联网上报、OTA、与云平台对接；若 MCU 只需「发到云」，此方案往往 **比通用 TCP AT** 更易维护。

---

### 方案 D：追求与乐鑫 ESP-AT 命令兼容

- **做法**：实现大量 `AT+CWJAP`、`AT+CIPMUX` 等与 **Wi‑Fi 模组** 强相关的命令。

**问题**：本设备 **不是** 作为 STA 芯片暴露给 MCU，而是 **路由器**；许多 ESP-AT 语义与当前架构 **不对齐**，且需长期跟进兼容与测试。

**复杂度**：**很高**。  
**实现可能性**：技术上可做，但 **投入产出比一般较差**，除非有明确「必须兼容某上位机软件」的商业约束。

---

## 6. 难度与复杂度评估（摘要）

| 维度 | 方案 B（自定义 TCP/UDP AT 子集） | 方案 D（兼容官方 ESP-AT） |
|------|----------------------------------|---------------------------|
| **协议设计** | 中：需文档化命令与错误码 | 高：需对照大量历史行为 |
| **固件实现** | 中：socket + 多路复用 + 缓冲 | 高：覆盖 Wi‑Fi/多连接/特殊模式 |
| **与路由/NAT 集成** | 低：沿用现有路由即可 | 中～高：语义对齐成本高 |
| **测试与维护** | 中：自测矩阵可控 | 高：兼容性与回归量大 |
| **RAM/PSRAM** | 中：每连接缓冲需上限 | 视功能而定，通常更高 |

**总体判断**：

- **「类 ESP-AT」= 路由器侧提供 AT 语法 + socket/MQTT 能力**：**可行性强，建议分期做方案 B 或 C**。  
- **「等于官方 ESP-AT」**：**不推荐**作为默认目标。

---

## 7. 实现时必须面对的工程问题

### 5.1 UART 与 `serial_cli`、日志

当前 **UART0** 上 **日志 + `serial_cli` + PCAPI** 已共存（见 `doc/命令.md`）。若 MCU 使用 **同一 UART**：

- **AT 会话** 与 **人机 CLI / PCAPI** 需 **前缀分流**（例如仅当行首为 `AT` 或进入 `AT+MODE` 后走 AT 状态机），否则易误解析；
- 或 **硬件上增加 UART1/UART2** 专供 MCU，**软件最简单、可靠性最高**（推荐在量产板上预留）。

### 5.2 安全

若 AT 可建连任意 IP/域名，**等价于在 LAN 侧开放代理能力**，需考虑：

- 是否仅限 **特定网段** 或 **白名单**；
- 管理密码与 **AT 通道是否绑定**（例如先 `AT+AUTH`）。

### 5.3 资源与实时性

多连接 + 大缓冲会占用 **RAM**；AT 解析任务优先级与 **网络任务** 的协调需避免 **长时间持锁** 阻塞协议栈。

---

## 8. 推荐实施路线（分阶段）

**阶段 1 — 协议与硬件**

- 定稿：**专用 UART** 还是与现有口 **分时复用**；
- 输出：`doc/` 下 **AT 命令说明**（可与 `ble_protocol` 并列风格）：命令、响应、`ERROR` 码、最大负载长度。

**阶段 2 — 最小可用**

- 实现 **单路 TCP 客户端** + `CIPSEND`/`+IPD` 类行为 + 基础状态查询；
- 联调：MCU 样例（可用 PC 串口助手模拟）。

**阶段 3 — 扩展**

- 多连接、UDP、超时与 **非阻塞读**；
- 可选：**MQTT AT** 或 **HTTPS 受限客户端**。

**阶段 4 — 运维**

- PC 工具或脚本回归；与 **工作模式切换**、**WAN 断开重连** 场景的长跑测试。

---

## 9. 结论

| 问题 | 结论 |
|------|------|
| **是否有必要做？** | 若产品需要 **裸机 MCU 快速上网** 且不想在 MCU 跑完整协议栈，**有价值**；本机 **已具备多接入与 NAT**，缺的是 **标准化 MCU 接口**。 |
| **难度** | **中等**（方案 B/C）；**极高**（强行兼容官方 ESP-AT 全集）。 |
| **复杂度** | 主要来自 **多连接 socket 管理、缓冲、UART 复用与安全**，而非 4G/Wi‑Fi 驱动本身。 |
| **实现可能性** | **高**；建议采用 **自定义 AT 子集 + 清晰文档**，而非绑定官方 ESP-AT 全兼容。 |

---

## 10. 参考与延伸

- 乐鑫官方文档：**[ESP-AT User Guide — AT Command Set](https://docs.espressif.com/projects/esp-at/en/latest/AT_Command_Set/index.html)**（指令以最新版为准；本文第 2 节表格为分层归纳，非全量逐条手册）。  
- 工程内：**`components/serial_cli`**、`doc/命令.md`、**PCAPI** 设计（行协议 + 回环 HTTP）可复用其「**一行一事务**」经验。  
- 乐鑫组件：**esp-modem** 中 **modem TCP over AT** 示例（`modem_tcp_client`）体现 **AT 与 socket 分层** 的思路，可类比到「MCU ↔ 路由器」方向（角色对调，但分层思想相通）。  
- 若未来需 **无线配网**，可继续用现有 **BLE JSON** 或 Web，而不必塞进 UART AT，以降低耦合。

---

## 11. 基于《at指令说明》的本项目实现范围（附指令清单）

以下结合根目录 **[`at指令说明.md`](at指令说明.md)**（乐鑫 ESP-AT 中文文档汇总表）与本机 **路由器 + 多 WAN + 已有 Web/BLE 管理** 的定位，划定 **不实现** 的文档章节，并给出 **大体可实现**（可模仿语义、分期落地）的 AT 指令范围。

### 11.1 明确不考虑的文档章节（本路由器不实现）

与 [`at指令说明.md`](at指令说明.md) 中下列章节对应的命令集 **整体不作为 MCU 串口 AT 的实现目标**（避免与现有产品路径重复或硬件角色不符）：

| 《at指令说明》章节 | 理由（摘要） |
|--------------------|--------------|
| **Bluetooth® Low Energy 命令集** | 本工程已有 **`ble_settings`（GATT + JSON）** 配网/参数通道；再实现一套 BLE AT 维护成本高、易重复。 |
| **经典 Bluetooth® AT 命令集** | 无经典蓝牙上位机场景；与路由核心能力无关。 |
| **文件系统 AT 命令集**（`AT+FS*`） | 与 LittleFS/Web 资源管理路径不同；MCU 侧一般不需要经 AT 做文件级访问。 |
| **ESP32 以太网 AT 命令**（`AT+CIPETH*`） | 以太网由 **工作模式 / bridge** 统一管理；非「给 MCU 当以太网网卡模组」形态。 |
| **信令测试 AT 命令** | 产测/射频专用，非路由业务。 |
| **驱动 AT 命令**（ADC/PWM/I2C/SPI） | 外设应由主板 MCU 直接驱动，不宜绑在路由 AT 上。 |
| **Web 服务器 AT 命令**（`AT+WEBSERVER`） | 设备侧已有 **webService + 网页**；无需再开一套 AT 控制的配网 Web。 |

### 11.2 纳入考虑范围的文档章节

仍可作为 **「类 ESP-AT」参考** 的章节：**基础 AT**、**Wi‑Fi AT**、**TCP/IP AT**、**MQTT AT**、**HTTP AT**、**WebSocket AT**。其中 **Wi‑Fi AT** 与路由控制面重叠大，通常 **只做极小集或不实现**（见 11.4）。

### 11.3 大体可实现的指令清单（按落地优先级）

下列命令名与语义对齐 [`at指令说明.md`](at指令说明.md) 及乐鑫文档；**本机可实现的是「语义」**，不保证与官方固件 **字节级兼容**。建议 **专用 UART** 或 **与 `serial_cli` 严格分流**。

**（1）第一期（MCU 上网核心）：TCP/IP 子集**

| 指令 | 说明 |
|------|------|
| `AT+CIPMUX` | 多连接开关 |
| `AT+CIPSTART` | 建立 TCP/UDP/SSL 客户端连接 |
| `AT+CIPCLOSE` | 关闭连接 |
| `AT+CIPSEND` | 发送数据（含 `>` 交互流程） |
| `AT+CIPDOMAIN` | 域名解析 |
| `AT+PING` | 连通性探测 |
| `AT+CIPSTATE` / `AT+CIFSR` | 连接信息 / 本地 IP（可与路由 LAN/WAN 摘要结合） |
| `AT+CIPDNS` | DNS 查询或设置（若与系统策略一致） |
| 主动上报 `+IPD` | 下行数据上报（行为对齐 ESP-AT 约定） |

**（2）第二期（长数据 / 被动收包 / 进阶套接字）**

| 指令 | 说明 |
|------|------|
| `AT+CIPSTARTEX` | 自动分配 link ID 建连 |
| `AT+CIPSENDEX` / `AT+CIPSENDL` / `AT+CIPSENDLCFG` | 扩展发送、长数据并行发送及配置 |
| `AT+CIPRECVTYPE` / `AT+CIPRECVDATA` / `AT+CIPRECVLEN` | 被动接收模式 |
| `AT+CIPMODE` / `AT+CIPRECONNINTV` | 透传模式与重连间隔（需与 UART 互斥策略一并设计） |
| `AT+CIPSERVER` / `AT+CIPSERVERMAXCONN` / `AT+CIPSTO` | 本机作 TCP/SSL 服务端（资源允许时） |
| `AT+CIPTCPOPT` | 套接字选项 |
| `AT+CIPDINFO` | 控制 `+IPD` 是否带远端信息 |
| `AT+CIPV6` | IPv6（若数据面已统一支持） |
| `AT+MDNS` | mDNS（按需） |
| `AT+CIPSSLCCONF` 及同族 `AT+CIPSSLC*` | SSL 客户端证书/TLS 参数（与 RAM、证书存储强相关） |

**（3）第三期（应用协议，按需）**

| 指令 | 说明 |
|------|------|
| `AT+MQTTUSERCFG` … `AT+MQTTCLEAN` | MQTT 全套（见 `at指令说明` §6） |
| `AT+HTTPCLIENT` … `AT+HTTPCFG` | HTTP 客户端全套（见 `at指令说明` §7） |
| `AT+WSCFG` … `AT+WSCLOSE` | WebSocket（见 `at指令说明` §9） |
| `AT+CIPSNTPCFG` / `AT+CIPSNTPTIME` / `AT+CIPSNTPINTV` | SNTP 与时间（可与现有系统时间逻辑整合） |
| `AT+CIUPDATE` / `AT+CIPFWVER` | 经网络的固件查询/升级（**需与现有 OTA/升级流程严格区分**，避免重复机制） |

**（4）基础 AT（极小运维集）**

| 指令 | 说明 |
|------|------|
| `AT` | 握手 |
| `AT+GMR` | 版本信息 |
| `ATE` | 回显开关（调试） |
| `AT+SYSRAM` | 堆占用（排障，可选） |

其余基础命令如 `AT+RST`、`AT+RESTORE`、`AT+GSLP`、`AT+SLEEP*`、`AT+UART_*`、`AT+SYSFLASH*`、`AT+SYSMFG`、`AT+RFPOWER`、`AT+RFCAL` 等，**与路由产品形态强相关**，建议 **单独产品决策**；`AT+SAVETRANSLINK` 在官方文档中含 **Bluetooth LE 透传**，本机若不实现 BLE AT，则 **仅可化用「Network 透传」语义** 或 **不实现**。

**（5）Wi‑Fi AT（默认不实现；例外）**

| 策略 | 说明 |
|------|------|
| **默认** | **不实现** `AT+CWJAP` / `AT+CWMODE` 等全套（与 **网页/工作模式/NVS** 冲突）。 |
| **若将来仅需「状态只读」** | 可评估极小集如 `AT+CWSTATE?` 式查询，映射到本机 dashboard 已有信息；**非第一期范围**。 |

### 11.4 简要分析

- **排除七类章节** 后，剩余价值集中在 **TCP/IP + 可选 MQTT/HTTP/WebSocket + 少量基础 AT**，与第 3 节「优先模仿 L2」一致。  
- **实现工作量**仍主要在：**多连接状态机、`+IPD` 与 UART 流控、TLS 与 RAM、与现有 `serial_cli`/日志共存**。  
- **不要**把 [`at指令说明.md`](at指令说明.md) 中已排除章节的命令名 **原样承诺**给 MCU 客户；若需 BluFi/配网，继续走 **BLE JSON / Web**。  
- 详细命令表仍以 [`at指令说明.md`](at指令说明.md) 为准；本节清单用于 **本仓库分期立项**，随实现进度在 `doc/` 中可再收敛为「正式 AT 手册」。

---

*文档版本：与当前仓库架构（iot_bridge、webService、serial_cli）一致；ESP-AT 指令分层与第 3 节「可模仿性」随官方文档演进，以乐鑫最新说明为准；**第 11 节** 与 [`at指令说明.md`](at指令说明.md) 联动界定本项目实现边界；具体命令集以实际立项时的 `doc` 为准。*
