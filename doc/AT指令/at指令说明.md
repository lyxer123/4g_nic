# AT 指令说明（乐鑫 ESP-AT 中文文档汇总）

本文档根据乐鑫 **[ESP-AT 用户指南 · AT 命令集（简体中文 · ESP32）](https://docs.espressif.com/projects/esp-at/zh_CN/latest/esp32/AT_Command_Set/index.html)** 各子页面 **「介绍」** 中的条目整理为表格，便于查阅。  

**完整语法、参数、示例、默认值与固件裁剪差异** 请以官方在线页面为准；本仓库 **4G NIC 路由器固件不保证** 实现下列任一条命令（实现范围见 **[`at指令实现.md`](at指令实现.md)**）。

---

## 文档索引（官方子页面）

| 章节（官方名称） | 文档路径（`…/AT_Command_Set/` 下） |
|------------------|-------------------------------------|
| 基础 AT 命令集 | `Basic_AT_Commands.html` |
| Wi-Fi AT 命令集 | `Wi-Fi_AT_Commands.html` |
| TCP/IP AT 命令 | `TCP-IP_AT_Commands.html` |
| Bluetooth® Low Energy 命令集 | `BLE_AT_Commands.html` |
| Bluetooth® AT 命令集（经典蓝牙） | `BT_AT_Commands.html` |
| MQTT AT 命令集 | `MQTT_AT_Commands.html` |
| HTTP AT 命令集 | `HTTP_AT_Commands.html` |
| 文件系统 AT 命令集 | `FS_AT_Commands.html` |
| WebSocket AT 命令集 | `websocket_at_commands.html` |
| 以太网 AT 命令 | `Ethernet_AT_Commands.html` |
| 信令测试 AT 命令 | `Signaling_Test_AT_Commands.html` |
| 驱动 AT 命令 | `Driver_AT_Commands.html` |
| Web 服务器 AT 命令 | `Web_server_AT_Commands.html` |

**说明**：官方左侧目录中的 **「用户 AT 命令集」** 等内容可能随版本调整路径；若链接变化，请以 [AT 命令集索引页](https://docs.espressif.com/projects/esp-at/zh_CN/latest/esp32/AT_Command_Set/index.html) 为准。

---

## 1. 基础 AT 命令集

| 指令 | 说明 |
|------|------|
| `AT` | 测试 AT 启动 |
| `AT+RST` | 重启模块 |
| `AT+GMR` | 查看版本信息 |
| `AT+CMD` | 查询当前固件支持的所有命令及命令类型 |
| `AT+GSLP` | 进入 Deep-sleep 模式 |
| `ATE` | 开启或关闭 AT 回显功能 |
| `AT+RESTORE` | 恢复出厂设置 |
| `AT+SAVETRANSLINK` | 设置开机 Network / Bluetooth LE 透传模式信息 |
| `AT+TRANSINTVL` | 设置透传模式下的数据发送间隔 |
| `AT+UART_CUR` | 设置 UART 当前临时配置，不保存到 flash |
| `AT+UART_DEF` | 设置 UART 默认配置，保存到 flash |
| `AT+SLEEP` | 设置睡眠模式 |
| `AT+SYSRAM` | 查询堆空间使用情况 |
| `AT+SYSMSG` | 查询/设置系统提示信息 |
| `AT+SYSMSGFILTER` | 启用或禁用系统消息过滤 |
| `AT+SYSMSGFILTERCFG` | 查询/配置系统消息的过滤器 |
| `AT+SYSFLASH` | 查询或读写 flash 用户分区 |
| `AT+SYSMFG` | 查询或读写 manufacturing nvs 用户分区 |
| `AT+RFPOWER` | 查询/设置 RF TX Power |
| `AT+RFCAL` | RF 全面校准 |
| `AT+SYSROLLBACK` | 回滚到以前的固件 |
| `AT+SYSTIMESTAMP` | 查询/设置本地时间戳 |
| `AT+SYSLOG` | 启用或禁用 AT 错误代码提示 |
| `AT+SLEEPWKCFG` | 设置 Light-sleep 唤醒源和唤醒 GPIO |
| `AT+SYSSTORE` | 设置参数存储模式 |
| `AT+SYSREG` | 读写寄存器 |

---

## 2. Wi-Fi AT 命令集

| 指令 | 说明 |
|------|------|
| `AT+CWINIT` | 初始化/清理 Wi-Fi 驱动程序 |
| `AT+CWMODE` | 查询/设置 Wi-Fi 模式 (Station/SoftAP/Station+SoftAP) |
| `AT+CWBANDWIDTH` | 查询/设置 Wi-Fi 带宽 |
| `AT+CWSTATE` | 查询 Wi-Fi 状态和 Wi-Fi 信息 |
| `AT+CWCONFIG` | 查询/设置 Wi-Fi 非活动时间和监听间隔时间 |
| `AT+CWJAP` | 连接 AP |
| `AT+CWRECONNCFG` | 查询/设置 Wi-Fi 重连配置 |
| `AT+CWLAPOPT` | 设置 AT+CWLAP 命令扫描结果的属性 |
| `AT+CWLAP` | 扫描当前可用的 AP |
| `AT+CWQAP` | 断开与 AP 的连接 |
| `AT+CWSAP` | 配置 ESP32 SoftAP 参数 |
| `AT+CWLIF` | 查询连接到 ESP32 SoftAP 的 station 信息 |
| `AT+CWQIF` | 断开 station 与 ESP32 SoftAP 的连接 |
| `AT+CWDHCP` | 启用或禁用 DHCP |
| `AT+CWDHCPS` | 查询/设置 ESP32 SoftAP DHCP 分配的 IPv4 地址范围 |
| `AT+CWAUTOCONN` | 上电是否自动连接 AP |
| `AT+CWAPPROTO` | 查询/设置 SoftAP 模式下 Wi-Fi 协议标准 |
| `AT+CWSTAPROTO` | 设置 Station 模式下 Wi-Fi 协议标准 |
| `AT+CIPSTAMAC` | 查询/设置 ESP32 Station 的 MAC 地址 |
| `AT+CIPAPMAC` | 查询/设置 ESP32 SoftAP 的 MAC 地址 |
| `AT+CIPSTA` | 查询/设置 ESP32 Station 的 IP 地址 |
| `AT+CIPAP` | 查询/设置 ESP32 SoftAP 的 IP 地址 |
| `AT+CWSTARTSMART` | 开启 SmartConfig |
| `AT+CWSTOPSMART` | 停止 SmartConfig |
| `AT+WPS` | 设置 WPS 功能 |
| `AT+CWJEAP` | 连接 WPA2 企业版 AP |
| `AT+CWHOSTNAME` | 查询/设置 ESP32 Station 的主机名称 |
| `AT+CWCOUNTRY` | 查询/设置 Wi-Fi 国家代码 |

---

## 3. TCP/IP AT 命令

以下为官方「介绍」列举顺序；其中 **`AT+CIPSENDEX` / `AT+CIPSENDL`** 在同页后续章节定义（介绍段未逐条列出时，按章节标题补入）。

| 指令 | 说明 |
|------|------|
| `AT+CIPV6` | 启用或禁用 IPv6 网络 (IPv6) |
| `AT+CIPSTATE` | 查询 TCP/UDP/SSL 连接信息 |
| `AT+CIPDOMAIN` | 域名解析 |
| `AT+CIPSTART` | 建立 TCP 连接、UDP 传输或 SSL 连接 |
| `AT+CIPSTARTEX` | 建立自动分配 ID 的 TCP 连接、UDP 传输或 SSL 连接 |
| `AT+SAVETRANSLINK` | 设置 Network 开机透传模式信息（亦见基础命令集） |
| `AT+CIPSEND` | 在普通传输模式或 Network 透传模式下发送数据 |
| `AT+CIPSENDEX` | 在普通传输模式下采用扩展的方式发送数据 |
| `AT+CIPSENDL` | 在普通传输模式下并行发送长数据 |
| `AT+CIPSENDLCFG` | 设置 AT+CIPSENDL 命令的属性 |
| `AT+CIPCLOSE` | 关闭 TCP/UDP/SSL 连接 |
| `AT+CIPCONNPERSIST` | 查询、设置 TCP/SSL 连接持久化参数 |
| `AT+CIFSR` | 查询本地 IP 地址和 MAC 地址 |
| `AT+CIPMUX` | 启用或禁用多连接模式 |
| `AT+CIPSERVER` | 建立/关闭 TCP 或 SSL 服务器 |
| `AT+CIPSERVERMAXCONN` | 查询/设置服务器允许建立的最大连接数 |
| `AT+CIPMODE` | 查询/设置传输模式 |
| `AT+CIPSTO` | 查询/设置本地 TCP 服务器超时时间 |
| `AT+CIPSNTPCFG` | 查询/设置时区和 SNTP 服务器 |
| `AT+CIPSNTPTIME` | 查询 SNTP 时间 |
| `AT+CIPSNTPINTV` | 查询/设置 SNTP 时间同步的间隔 |
| `AT+CIPFWVER` | 查询服务器已有的 AT 固件版本 |
| `AT+CIUPDATE` | 通过 Network 升级固件 |
| `AT+CIPDINFO` | 设置 +IPD 消息详情 |
| `AT+CIPSSLCCONF` | 查询/设置 SSL 客户端配置 |
| `AT+CIPSSLCCIPHER` | 查询/设置 SSL 客户端的密码套件 (cipher suite) |
| `AT+CIPSSLCCN` | 查询/设置 SSL 客户端的公用名 (common name) |
| `AT+CIPSSLCSNI` | 查询/设置 SSL 客户端的 SNI |
| `AT+CIPSSLCALPN` | 查询/设置 SSL 客户端 ALPN |
| `AT+CIPSSLCPSK` | 查询/设置 SSL 客户端的 PSK（字符串格式） |
| `AT+CIPSSLCPSKHEX` | 查询/设置 SSL 客户端的 PSK（十六进制格式） |
| `AT+CIPRECONNINTV` | 查询/设置 Network 透传模式下的 TCP/UDP/SSL 重连间隔 |
| `AT+CIPRECVTYPE` | 查询/设置套接字接收模式 |
| `AT+CIPRECVDATA` | 获取被动接收模式下的套接字数据 |
| `AT+CIPRECVLEN` | 查询被动接收模式下套接字数据的长度 |
| `AT+PING` | ping 对端主机 |
| `AT+CIPDNS` | 查询/设置 DNS 服务器信息 |
| `AT+MDNS` | 设置 mDNS 功能 |
| `AT+CIPTCPOPT` | 查询/设置套接字选项 |

---

## 4. Bluetooth® Low Energy 命令集

| 指令 | 说明 |
|------|------|
| `AT+BLEINIT` | Bluetooth LE 初始化 |
| `AT+BLEADDR` | 设置 Bluetooth LE 设备地址 |
| `AT+BLENAME` | 查询/设置 Bluetooth LE 设备名称 |
| `AT+BLESCANPARAM` | 查询/设置 Bluetooth LE 扫描参数 |
| `AT+BLESCAN` | 使能 Bluetooth LE 扫描 |
| `AT+BLESCANRSPDATA` | 设置 Bluetooth LE 扫描响应 |
| `AT+BLEADVPARAM` | 查询/设置 Bluetooth LE 广播参数 |
| `AT+BLEADVDATA` | 设置 Bluetooth LE 广播数据 |
| `AT+BLEADVDATAEX` | 自动设置 Bluetooth LE 广播数据 |
| `AT+BLEADVSTART` | 开始 Bluetooth LE 广播 |
| `AT+BLEADVSTOP` | 停止 Bluetooth LE 广播 |
| `AT+BLECONN` | 建立 Bluetooth LE 连接 |
| `AT+BLECONNPARAM` | 查询/更新 Bluetooth LE 连接参数 |
| `AT+BLEDISCONN` | 断开 Bluetooth LE 连接 |
| `AT+BLEDATALEN` | 设置 Bluetooth LE 数据包长度 |
| `AT+BLECFGMTU` | 设置 Bluetooth LE MTU 长度 |
| `AT+BLEGATTSSRVCRE` | GATTS 创建服务 |
| `AT+BLEGATTSSRVSTART` | GATTS 开启服务 |
| `AT+BLEGATTSSRVSTOP` | GATTS 停止服务 |
| `AT+BLEGATTSSRV` | GATTS 发现服务 |
| `AT+BLEGATTSCHAR` | GATTS 发现服务特征 |
| `AT+BLEGATTSNTFY` | 服务器 notify 服务特征值给客户端 |
| `AT+BLEGATTSIND` | 服务器 indicate 服务特征值给客户端 |
| `AT+BLEGATTSSETATTR` | GATTS 设置服务特征值 |
| `AT+BLEGATTCPRIMSRV` | GATTC 发现基本服务 |
| `AT+BLEGATTCINCLSRV` | GATTC 发现包含的服务 |
| `AT+BLEGATTCCHAR` | GATTC 发现服务特征 |
| `AT+BLEGATTCRD` | GATTC 读取服务特征值 |
| `AT+BLEGATTCWR` | GATTC 写服务特征值 |
| `AT+BLESPPCFG` | 查询/设置 Bluetooth LE SPP 参数 |
| `AT+BLESPP` | 进入 Bluetooth LE SPP 模式 |
| `AT+SAVETRANSLINK` | 设置 Bluetooth LE 开机透传模式信息（亦见基础命令集） |
| `AT+BLESECPARAM` | 查询/设置 Bluetooth LE 加密参数 |
| `AT+BLEENC` | 发起 Bluetooth LE 加密请求 |
| `AT+BLEENCRSP` | 回复对端设备发起的配对请求 |
| `AT+BLEKEYREPLY` | 给对方设备回复密钥 |
| `AT+BLECONFREPLY` | 给对方设备回复确认结果（传统连接阶段） |
| `AT+BLEENCDEV` | 查询绑定的 Bluetooth LE 加密设备列表 |
| `AT+BLEENCCLEAR` | 清除 Bluetooth LE 加密设备列表 |
| `AT+BLESETKEY` | 设置 Bluetooth LE 静态配对密钥 |
| `AT+BLEHIDINIT` | Bluetooth LE HID 协议初始化 |
| `AT+BLEHIDKB` | 发送 Bluetooth LE HID 键盘信息 |
| `AT+BLEHIDMUS` | 发送 Bluetooth LE HID 鼠标信息 |
| `AT+BLEHIDCONSUMER` | 发送 Bluetooth LE HID consumer 信息 |
| `AT+BLUFI` | 开启或关闭 BluFi |
| `AT+BLUFINAME` | 查询/设置 BluFi 设备名称 |
| `AT+BLUFISEND` | 发送 BluFi 用户自定义数据 |
| `AT+BLERDRSSI` | 查询当前连接的 RSSI |
| `AT+BLEWL` | 设置白名单 |

---

## 5. ESP32 经典 Bluetooth® AT 命令集

| 指令 | 说明 |
|------|------|
| `AT+BTINIT` | Classic Bluetooth 初始化 |
| `AT+BTNAME` | 查询/设置 Classic Bluetooth 设备名称 |
| `AT+BTSCANMODE` | 设置 Classic Bluetooth 扫描模式 |
| `AT+BTSTARTDISC` | 开始发现周边 Classic Bluetooth 设备 |
| `AT+BTSPPINIT` | Classic Bluetooth SPP 协议初始化 |
| `AT+BTSPPCONN` | 查询/建立 SPP 连接 |
| `AT+BTSPPDISCONN` | 断开 SPP 连接 |
| `AT+BTSPPSTART` | 开启 Classic Bluetooth SPP 协议 |
| `AT+BTSPPSEND` | 发送数据到对方 Classic Bluetooth SPP 设备 |
| `AT+BTA2DPINIT` | Classic Bluetooth A2DP 协议初始化 |
| `AT+BTA2DPCONN` | 查询/建立 A2DP 连接 |
| `AT+BTA2DPDISCONN` | 断开 A2DP 连接 |
| `AT+BTA2DPSRC` | 查询/设置音频文件 URL |
| `AT+BTA2DPCTRL` | 控制音频播放 |
| `AT+BTSECPARAM` | 查询/设置 Classic Bluetooth 安全参数 |
| `AT+BTKEYREPLY` | 输入简单配对密钥 (Simple Pair Key) |
| `AT+BTPINREPLY` | 输入传统配对密码 (Legacy Pair PIN Code) |
| `AT+BTSECCFM` | 给对方设备回复确认结果（传统连接阶段） |
| `AT+BTENCDEV` | 查询 Classic Bluetooth 加密设备列表 |
| `AT+BTENCCLEAR` | 清除 Classic Bluetooth 加密设备列表 |
| `AT+BTCOD` | 设置设备类型 |
| `AT+BTPOWER` | 查询/设置 Classic Bluetooth 的 TX 功率 |

---

## 6. MQTT AT 命令集

| 指令 | 说明 |
|------|------|
| `AT+MQTTUSERCFG` | 设置 MQTT 用户属性 |
| `AT+MQTTLONGCLIENTID` | 设置 MQTT 客户端 ID |
| `AT+MQTTLONGUSERNAME` | 设置 MQTT 登陆用户名 |
| `AT+MQTTLONGPASSWORD` | 设置 MQTT 登陆密码 |
| `AT+MQTTCONNCFG` | 设置 MQTT 连接属性 |
| `AT+MQTTALPN` | 设置 MQTT 应用层协议协商 (ALPN) |
| `AT+MQTTSNI` | 设置 MQTT 服务器名称指示 (SNI) |
| `AT+MQTTCONN` | 连接 MQTT Broker |
| `AT+MQTTPUB` | 发布 MQTT 消息（字符串） |
| `AT+MQTTPUBRAW` | 发布长 MQTT 消息 |
| `AT+MQTTSUB` | 订阅 MQTT Topic |
| `AT+MQTTUNSUB` | 取消订阅 MQTT Topic |
| `AT+MQTTCLEAN` | 断开 MQTT 连接 |

*同页另含 **MQTT AT 错误码**、**MQTT AT 说明**（主动上报等），见官方文档正文。*

---

## 7. HTTP AT 命令集

| 指令 | 说明 |
|------|------|
| `AT+HTTPCLIENT` | 发送 HTTP 客户端请求 |
| `AT+HTTPGETSIZE` | 获取 HTTP 资源大小 |
| `AT+HTTPCGET` | 获取 HTTP 资源 |
| `AT+HTTPCPOST` | Post 指定长度的 HTTP 数据 |
| `AT+HTTPCPUT` | Put 指定长度的 HTTP 数据 |
| `AT+HTTPURLCFG` | 设置/获取长的 HTTP URL |
| `AT+HTTPCHEAD` | 设置/查询 HTTP 请求头 |
| `AT+HTTPCFG` | 设置 HTTP 客户端配置 |

*同页另含 **HTTP AT 错误码**，见官方文档正文。*

---

## 8. 文件系统 AT 命令集

| 指令 | 说明 |
|------|------|
| `AT+FS` | 文件系统操作 |
| `AT+FSMOUNT` | 挂载/卸载文件系统 |

---

## 9. WebSocket AT 命令集

| 指令 | 说明 |
|------|------|
| `AT+WSCFG` | 配置 WebSocket 参数 |
| `AT+WSHEAD` | 设置/查询 WebSocket 请求头 |
| `AT+WSOPEN` | 查询/打开 WebSocket 连接 |
| `AT+WSSEND` | 向 WebSocket 连接发送数据 |
| `AT+WSDATAFMT` | 设置 WebSocket 接收数据格式 |
| `AT+WSCLOSE` | 关闭 WebSocket 连接 |

---

## 10. ESP32 以太网 AT 命令

| 指令 | 说明 |
|------|------|
| `AT+CIPETHMAC` | 查询/设置 ESP32 以太网的 MAC 地址 |
| `AT+CIPETH` | 查询/设置 ESP32 以太网的 IP 地址 |

---

## 11. 信令测试 AT 命令

| 指令 | 说明 |
|------|------|
| `AT+FACTPLCP` | 发送长 PLCP 或短 PLCP |

---

## 12. 驱动 AT 命令

| 指令 | 说明 |
|------|------|
| `AT+DRVADC` | 读取 ADC 通道值 |
| `AT+DRVPWMINIT` | 初始化 PWM 驱动器 |
| `AT+DRVPWMDUTY` | 设置 PWM 占空比 |
| `AT+DRVPWMFADE` | 设置 PWM 渐变 |
| `AT+DRVI2CINIT` | 初始化 I2C 主机驱动 |
| `AT+DRVI2CRD` | 读取 I2C 数据 |
| `AT+DRVI2CWRDATA` | 写入 I2C 数据 |
| `AT+DRVI2CWRBYTES` | 写入不超过 4 字节的 I2C 数据 |
| `AT+DRVSPICONFGPIO` | 配置 SPI GPIO |
| `AT+DRVSPIINIT` | 初始化 SPI 主机驱动 |
| `AT+DRVSPIRD` | 读取 SPI 数据 |
| `AT+DRVSPIWR` | 写入 SPI 数据 |

---

## 13. Web 服务器 AT 命令

| 指令 | 说明 |
|------|------|
| `AT+WEBSERVER` | 启用或禁用通过 Web 服务器配置 Wi-Fi 连接 |

---

## 附：索引页中的通用说明（非单条 AT）

官方 [AT 命令集索引](https://docs.espressif.com/projects/esp-at/zh_CN/latest/esp32/AT_Command_Set/index.html) 另含：**AT 命令类型**（测试/查询/设置/执行）、**参数写入 flash 的规则**、**AT 响应与主动消息**（如 `OK`、`ERROR`、`+IPD`、`WIFI GOT IP` 等）、**AT 日志** 等，此处不展开成表。

---

*整理说明：表格内容对齐乐鑫 ESP-AT 中文文档各子页「介绍」与必要章节标题；若官方更新命令增减或更名，请以在线文档为准。*
