# 4G NIC 工程说明（中文）

本仓库在 **Espressif esp-iot-bridge（`espressif/iot_bridge`）** 示例基础上，实现 **4G（PPP）/ Wi‑Fi / 有线** 等接口组合下的 **NAT 转发**，并集成 **网页管理**、**串口 CLI + PCAPI**、**工作模式管理** 及配套 **PC / 手机端工具**。  
图示拓扑仍以官方 **4G NIC** 示意为准：多接口经 ESP 转发至 PPP 侧。

![4g_nic_topology](https://raw.githubusercontent.com/espressif/esp-iot-bridge/master/components/iot_bridge/docs/_static/4g_nic_en.png)

## 版本信息

| 项目 | 说明 |
|------|------|
| ESP-IDF | **v5.2.6**（当前工程主要验证版本） |
| ESP-IoT-Bridge | **1.0.2**（`managed_components` 中 `espressif__iot_bridge`） |

历史调试若涉及 **ESP-IDF v5.1.x** 对照，见根目录 **[`调试.md`](调试.md)**（含 W5500、PPP、SoftAP 网段与 bridge 补丁说明）。

| 支持目标 | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-C5 | ESP32-S2 | ESP32-S3 |
| --- | --- | --- | --- | --- | --- | --- | --- |

## 本仓库功能结构

| 模块 | 作用 |
|------|------|
| **iot_bridge** | 4G（PPP）、Wi‑Fi STA/SoftAP、以太网（内置 EMAC / **SPI W5500** / SDIO 等，以 menuconfig 为准）之间与 **NAPT** 相关的数据面。 |
| **`components/system`** | 硬件探测、**工作模式（work_mode）** 配置与延期应用、双 Wi‑Fi 连接、STA 探测 / 有线调试、**周期性堆内存日志**（`system_stability`，可 Kconfig 关闭）等。 |
| **`components/webService`** | 设备端 **HTTP 服务**：与 `webPage` 一致的 **REST API**；LittleFS 静态资源；支持串口 **PCAPI** 回环到本机 HTTP 处理逻辑。 |
| **`components/serial_cli`** | **UART**：交互命令（`help`、`ping`、`mode_get` / `mode_set` 等）及 **PCAPI**（PC 工具经串口发 HTTP 样式请求，见 [`doc/命令.md`](doc/命令.md)）。 |
| **`components/ble_settings`** | **BLE GATT + JSON** 配网/参数通道（协议见 [`doc/ble_protocolcn.md`](doc/ble_protocolcn.md)）。若需上电即开蓝牙，请在 `app_main` 中自行调用 `ble_settings_start()` 并满足 Kconfig/依赖。 |
| **`webPage/`** | 网页前端；发布到固件分区见 [`doc/webdeploy/`](doc/webdeploy/)。 |
| **`PCSoftware/`** | Windows 上 **Python + Tk** 管理台：与浏览器 **同一套 API**，经串口 PCAPI 访问设备；支持 **PyInstaller** 打包为 **`4G_NIC_Admin.exe`**（见 [`PCSoftware/README.md`](PCSoftware/README.md)）。 |
| **`app/`** | **Flutter** 示例（面向 BLE），见 [`app/README.md`](app/README.md)。 |

启动顺序（概要）：NVS → 网络栈与事件 → 硬件探测 → 按硬件创建 bridge 网卡 → 应用保存的 **工作模式** → Wi‑Fi 相关初始化 → **稳定性初始化** → **Web 服务** → **串口 CLI**。

## 稳定性与资源（分析说明与固件策略）

### 问题分析：哪些因素容易导致「假死、跑久了不转发」

| 现象/根因 | 说明 |
|-----------|------|
| **看门狗长期关闭** | 若某任务死锁或陷入长时间忙等，CPU 可能一直不恢复，用户只能断电；表现为设备「卡死」、串口无新日志。 |
| **仅片内 SRAM、高负载** | **4G PPP + Wi‑Fi + NAPT** 同时工作时，lwIP / WiFi 大量占用 **pbuf、邮箱、套接字**；片内堆紧张时易出现 **`pppos_input_tcpip failed (-1)`**（常与 **ERR_MEM**、TCPIP 邮箱满相关），或偶发分配失败，体感为卡顿、断网、难以复现。 |
| **lwIP 默认连接/套接字上限偏低** | 作为 **路由转发** 场景，会话数多于普通客户端；上限过低时更易在并发下耗尽资源。 |

以下情况 **不是** 仅靠改固件能根治：**SIM 欠费**、**运营商侧故障**、**USB 供电/线材差导致模组掉线**、**多 WAN 宏全开却无产品级选路策略**（DNS/默认路由易混乱，见 **`调试.md`** 第 5 节）。

### 本仓库采取的缓解措施

| 措施 | 目的 |
|------|------|
| **PSRAM（ESP32-S3，OPI Octal）** | 在 **`sdkconfig.defaults.esp32s3`** 中默认开启 `CONFIG_SPIRAM*`、`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` 等，把更多缓冲与分配导向 **外部 RAM**，减轻片内压力。**模组无 PSRAM 时必须注释整段 `CONFIG_SPIRAM*`**，再 **`idf.py fullclean`** 后编译，否则会启动失败。 |
| **中断 WDT + 任务 WDT** | 长时间死锁时由硬件 **复位芯片**，避免无限期挂死；任务超时约 **15s**，中断 WDT 约 **1000ms**。若 USB Host / PPP 路径误触发，可在 menuconfig 中适当 **加大超时**。 |
| **提高 lwIP 套接字 / TCP 数量** | 见 **`sdkconfig.defaults`**，降低 NAPT 高并发下「资源耗尽」概率；与已存在的 **TCPIP mbox**、**TCPIP 任务栈** 调整一起，缓解 PPP + Wi‑Fi 并存时的压力（详见 **`调试.md`** 与 `pppos_input_tcpip` 讨论）。 |
| **周期性堆日志（`system_stability`）** | menuconfig → **System options → Periodic heap log interval**，默认 **120s**：打印当前剩余堆、**历史最低剩余**（内部 RAM），若启用 PSRAM 则同时打印 PSRAM 剩余，便于发现 **慢泄漏**。设为 **0** 关闭。 |

修改 **`sdkconfig.defaults`** 或目标专用 defaults 后，建议 **`idf.py fullclean`** 或重新生成 **`sdkconfig`**，确保选项生效。

**注意（Windows）**：`sdkconfig.defaults` / `sdkconfig.defaults.<target>` 中的**注释请使用英文或纯 ASCII**。在中文系统下，若注释含中文/特殊 Unicode，部分环境下 `kconfgen` 用系统编码读取会触发 **`UnicodeDecodeError`**，导致 CMake 配置失败。

### 使用与运维建议

- **USB 4G**：尽量保证 **供电充足、线材可靠**，减少 PPP 层抖动与主机栈异常。  
- **多 WAN**：不要同时打开多个 `BRIDGE_EXTERNAL_NETIF_*` 却指望「自动择优」——bridge **无完整多 WAN 策略**，稳定场景宜 **单一主 uplink**。  
- **排障**：结合串口日志与周期性 **heap** 行，观察 `min_ever` 是否持续下降。

## 硬件与接口说明

**常见配置**

- ESP32 系列开发板（如 **ESP32-S3**）  
- **4G 模组**：USB（CDC）或 UART，与 `menuconfig` 中 Modem 配置一致  
- 供电需覆盖模组突发电流  

**可选**

- **网线 + W5500（SPI）**：对内转发口选 **SPI** 相关项；**SPI 与 SDIO 两类 Netif 不能同时启用**（与「用 W5500」不矛盾：W5500 走 SPI，勿再开 SDIO 转发）。  
- 杜邦线按原理图连接 SPI / 复位 / INT 等。  

## menuconfig 要点

1. **Modem**  
   `Component config → Bridge Configuration → Modem Configuration`  
   选择 **UART 或 USB**、APN、鉴权等。

2. **对外数据转发口（接 PC / 下游网段设备）**  
   `Component config → Bridge Configuration → The interface used to provide network data forwarding for other devices`  
   可选 **以太网 / SPI / SDIO / SoftAP / USB** 等（以实际 Kconfig 为准）。

3. **外网接口（WAN）**  
   在 Bridge 中配置 **STA / Modem / 以太网 WAN** 等。多选宏可同时存在，但 **官方 bridge 并非多 WAN 负载均衡**；追求稳定建议 **明确单一主 uplink**，详见 [`调试.md`](调试.md) 第 5 节。

工程默认 lwIP/PPP 相关选项见 [`sdkconfig.defaults`](sdkconfig.defaults)（如 NAPT、TCPIP 邮箱、TCPIP 任务栈等）；修改后若与当前 `sdkconfig` 不一致，需 `fullclean` 或 menuconfig 对齐后再编译。

## 分区表

- 默认 **4MB** Flash：[`partitions_4mb.csv`](partitions_4mb.csv)  
- 大 Flash 板：[`partitions_large.csv`](partitions_large.csv)  

## 编译与烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

仅改应用时可缩短烧录时间：

```bash
idf.py app-flash monitor
```

退出串口监视器：**Ctrl+]**。

## 文档与工具索引

| 文档 | 内容 |
|------|------|
| [`README.md`](README.md) | 英文总览（与本文件对应）。 |
| [`调试.md`](调试.md) | 问题排查：PHY 复位、PPP `pppos_input_tcpip`、SoftAP 踢站、多 WAN 行为等。 |
| [`doc/命令.md`](doc/命令.md) | 串口 CLI 与 PCAPI；BLE JSON 摘要。 |
| [`doc/ble_protocol.md`](doc/ble_protocol.md) / [`doc/ble_protocolcn.md`](doc/ble_protocolcn.md) | BLE 特征与命令表。 |
| [`PCSoftware/AT指令说明.md`](PCSoftware/AT指令说明.md) | ESP32-S3 路由器固件实现的 AT 指令集说明。 |
| [`at指令实现.md`](at指令实现.md) | AT 指令架构设计与实现规划评估。 |
| [`at指令说明.md`](at指令说明.md) | 乐鑫官方 ESP-AT 指令集参考（中文）。 |
| [`PCSoftware/README.md`](PCSoftware/README.md) | PC 串口管理工具与 **exe 打包**。 |
| [`app/README.md`](app/README.md) | Flutter BLE 工程说明。 |

上游 bridge 通用说明：  
[User_Guide.md](https://github.com/espressif/esp-iot-bridge/blob/master/components/iot_bridge/User_Guide.md)（本地副本在 `managed_components/espressif__iot_bridge/User_Guide.md` / `User_Guide_CN.md`）。

## 许可证

各源文件 SPDX 声明为准（示例代码多为 Apache-2.0 等）。
