# esp-iot-bridge STA与SoftAP同时启用使用说明

## 序号1，STA连接外网+SoftAP转发完整使用说明，2026-04-01

### 1.1 什么问题
在 `esp-iot-bridge` 中同时勾选：

- `Use Wi-Fi station interface to connect to the external network`
- `Use Wi-Fi SoftAP interface to provide network data forwarding for other devices`

后，不清楚实际如何配置、如何连网、如何验证是否真的完成“上行 STA + 下行 SoftAP 转发”。

### 1.2 如何解决的
将该组合按“Wi-Fi 路由器”模式来使用：ESP 先用 `STA` 连接上级路由器（外网入口），再通过 `SoftAP` 给手机/PC 提供接入并转发网络。

#### A. menuconfig 必选项
在 `idf.py menuconfig` 中确认：

1. `Bridge Configuration` 里启用外网接口 `Wi-Fi station`。
2. `Bridge Configuration` 里启用转发接口 `Wi-Fi SoftAP`。
3. 在 `SoftAP Config` 里设置：
   - `SSID`
   - `Password`
   - `Channel`（可默认）
   - `Max Connection`（按需要）
4. （可选）启用 `BRIDGE_SOFTAP_SSID_END_WITH_THE_MAC`，便于区分多台设备。

> 说明：这个组合就是官方文档中的 Wi-Fi 路由器场景，允许“一个设备同时做 STA 上行 + AP 下行”。

#### B. 编译与烧录
```bash
idf.py build
idf.py flash
idf.py monitor
```

如果你已经熟悉流程，也可以一次执行：
```bash
idf.py build flash monitor
```

#### C. 首次使用配网（给 STA 配上级路由器）
因为 `STA` 需要知道“上级路由器 SSID/密码”，首次一般先通过 `SoftAP` 进入配网页面：

1. 手机或电脑连接 ESP 发出的 `SoftAP`（上面设置的 SSID）。
2. 获取到 IP 后，浏览器访问网关地址（常见是 `192.168.4.1`，以实际 DHCP 网关为准）。
3. 在网页里填写上级路由器（家庭/办公 Wi-Fi）的 SSID 和密码并保存。
4. 观察串口日志，确认 `STA` 成功连上上级路由并获取 IP。

也可以使用 BLE Provisioning（芯片支持 BLE 时）做配网。

#### D. 正常运行链路（你真正想要的效果）
运行成功后的数据路径如下：

`手机/PC -> ESP SoftAP -> ESP NAT/转发 -> ESP STA -> 上级路由器 -> Internet`

也就是说，终端设备只需要连 ESP 的热点，就能通过 ESP 的 STA 出口上网。

#### E. 验证是否成功
建议按下面顺序检查：

1. **看 STA 状态（串口日志）**
   - 是否出现 `connected` / `got ip` 之类日志。
2. **看 SoftAP 侧拿地址**
   - 手机/PC 连热点后是否获得 IP（例如 `192.168.4.x`）。
3. **看外网连通性**
   - 终端 `ping 8.8.8.8`
   - 或浏览器访问网页。
4. **看 DNS**
   - 能 ping IP 不能访问域名时，优先检查 DNS 配置。

#### F. 常见问题与处理
1. **STA 连不上上级 Wi-Fi**
   - 检查 SSID/密码；
   - 检查上级路由器信号强度与加密方式；
   - 确认芯片/国家区域设置与信道兼容。

2. **连上 SoftAP 但不能上网**
   - 先确认 STA 是否已拿到上级 IP；
   - 再检查是否存在网段冲突（上级网段和 SoftAP 网段重叠）；
   - 必要时修改 SoftAP 默认网段后重试。

3. **多设备连入后网速差**
   - SoftAP 共享 STA 带宽，属于正常现象；
   - 适当降低连接设备数或优化部署位置。

4. **配置后重启丢失**
   - 确认使用了组件默认的 NVS 存储流程；
   - 排查是否有擦除 NVS 的操作。

### 1.3 解决结果
已明确该组合的正确使用方式：

- 角色分工：`STA` 负责连接外网，`SoftAP` 负责给其他设备接入。
- 操作流程：`menuconfig` 启用双接口 -> 烧录运行 -> 先配 STA 上级 Wi-Fi -> 终端连 SoftAP 上网。
- 验证方法：按“STA拿IP、SoftAP分配IP、终端外网连通”三层检查即可快速定位问题。

该文档可直接作为后续同类配置的标准操作说明使用。

## 序号2，新增STA网页配置页面，2026-04-01

### 2.1 什么问题
希望在本项目根目录新建 `webPage` 文件夹，页面风格参考 `F:\github\Lua-uPC\webpage`，先实现 `Wi-Fi STA` 的 `SSID` 扫描、密码输入、保存与修改功能。

### 2.2 如何解决的
在本项目新增 `webPage` 前端页面并完成首版交互：

- 新建 `webPage/index.html`、`webPage/css/style.css`、`webPage/js/app.js`。
- 页面实现：
  - 点击“扫描”调用 `GET /api/wifi/scan`，展示可用 SSID 列表并支持点选回填。
  - 输入 `password` 后点击“保存/修改”，调用 `POST /api/wifi` 提交 `{ssid, password}`。
  - 点击“从设备读取”调用 `GET /api/wifi` 回显当前配置。
  - 在接口未就绪时自动回退到本地 `localStorage`，方便先联调页面。
- 增加 `webPage/README.md` 说明接口约定与使用方式。

### 2.3 解决结果
本项目已具备独立的 STA 配网页面基础能力，支持“扫描 SSID -> 输入密码 -> 保存/修改 -> 回显”，后续只需对接设备侧 API 即可完整闭环。

## 序号3，按Lua-uPC模式完成网页烧写与分区切换，2026-04-01

### 3.1 什么问题
要求模仿 `F:\github\Lua-uPC\doc\webdeploy` 的模式，对本项目 `webPage` 内容进行烧写；同时将 `F:\github\Lua-uPC\partitions_large.csv` 拷贝到本项目并启用该分区文件。

### 3.2 如何解决的
已完成以下落地：

1. 新增分区文件  
   - 复制为项目根目录：`partitions_large.csv`。

2. 新增网页烧写脚本（对齐 Lua-uPC 流程）  
   - 新建：`doc/webdeploy/deployweb.bat`  
   - 关键逻辑：
     - 从 `webPage/` 复制资源到 staging；
     - 对文件逐个 `gzip` 压缩；
     - 使用 `mklittlefs` 生成 `www.bin`；
     - 使用 `esptool` 按 `www` 分区偏移 `0xC76000` 烧写。

3. 切换项目分区配置为自定义分区  
   - 更新 `sdkconfig` 与 `sdkconfig.old`：
     - `CONFIG_PARTITION_TABLE_CUSTOM=y`
     - `CONFIG_PARTITION_TABLE_FILENAME="partitions_large.csv"`
     - `CONFIG_PARTITION_TABLE_OFFSET=0x10000`

### 3.3 解决结果
本项目已完成“网页资源打包烧写脚本 + 大分区表迁移 + 分区配置启用”的一整套改造，可直接按脚本流程烧写 `webPage` 到 `www` 分区。

## 序号4，热点IP与网页无法访问问题定位与修复，2026-04-01

### 4.1 什么问题
烧写 `webPage` 后，PC 连接 ESP32 热点时看到地址是 `192.168.4.2`（预期不是 `192.168.5.2`）；并且浏览器访问 `http://192.168.4.1/` 与 `http://192.168.5.1/` 都无法打开内置网页。

### 4.2 如何解决的
排查日志后确认有两点：

1. **IP 认知问题（正常现象）**  
   - 日志显示：`DHCP server started ... IP: 192.168.4.1`，这是 ESP SoftAP 网关地址。  
   - `192.168.4.2` 是 DHCP 分给 PC 的客户端地址，属于正常行为。  
   - 之所以不是 `192.168.5.x`，是因为当前 SoftAP 网段默认是 `192.168.4.0/24`，且日志里有 `NVS: Failed to read IP info from NVS`，说明未加载到自定义网段配置。

2. **网页无法访问的根因与修复**  
   - 根因：项目此前仅创建了网卡（STA/AP），并未挂载 `www` 分区，也未启动 HTTP 文件服务器。  
   - 修复内容：
     - 在 `main/app_main.c` 增加 LittleFS 挂载：`partition_label = "www"`，挂载点 `/www`；
     - 增加 `esp_http_server` 静态资源服务；
     - `GET /*` 路由映射到 `/www` 下文件，`/` 默认返回 `index.html`；
     - 兼容 `deployweb.bat` 产物（仅 `.gz` 文件）：若存在 `xxx.gz` 则返回并设置 `Content-Encoding: gzip`。
     - 在 `main/CMakeLists.txt` 增加依赖：`esp_http_server`、`esp_littlefs`。

### 4.3 解决结果
已完成代码修复。后续重新编译烧录并再次执行网页分区烧写后：

- PC 连接热点获取 `192.168.4.2` 属正常；
- 使用浏览器访问 `http://192.168.4.1/` 应可打开内置网页（`192.168.5.1` 不再作为默认入口）。

## 序号5，esp_littlefs依赖失败改为SPIFFS方案，2026-04-01

### 5.1 什么问题
编译时报错：

- `Failed to resolve component 'esp_littlefs' required by component 'webService': unknown name.`

说明当前环境中无法解析 `esp_littlefs` 组件（此前还出现过 registry 403），导致 `webService` 无法完成依赖解析。

### 5.2 如何解决的
将 Web 文件系统方案由 `LittleFS` 切换为 `SPIFFS`（ESP-IDF 内建组件）：

1. 组件依赖调整  
   - `components/webService/CMakeLists.txt`  
   - `REQUIRES` 从 `esp_littlefs` 改为 `spiffs`。

2. 挂载代码调整  
   - `components/webService/src/web_service.c`  
   - 头文件改为 `esp_spiffs.h`；  
   - 挂载 API 从 `esp_vfs_littlefs_register` 改为 `esp_vfs_spiffs_register`。

3. 分区表同步  
   - `partitions_large.csv` 中 `www` 与 `lua` 分区 subtype 从 `littlefs` 改为 `spiffs`。

4. 网页烧写脚本同步  
   - `doc/webdeploy/deployweb.bat` 改为使用 `mkspiffs.exe` 生成 `www.bin`；
   - 日志文案由 LittleFS 改为 SPIFFS。

### 5.3 解决结果
已移除对 `esp_littlefs` 的依赖链路，改用 SPIFFS 后可避免组件拉取失败导致的构建中断。后续按 SPIFFS 工具链（`mkspiffs`）烧写网页分区即可。

## 序号6，按要求全面切回LittleFS文件系统，2026-04-01

### 6.1 什么问题
当前方案改成 SPIFFS 后，用户要求恢复为 **LittleFS**，并指出现有 bat 部署流程与当前文件系统不匹配，需要全面回切。

### 6.2 如何解决的
已将 Web 存储链路完整切回 LittleFS，并修正部署脚本：

1. 组件依赖回切  
   - `components/webService/CMakeLists.txt`  
   - `REQUIRES` 从 `spiffs` 改回 `esp_littlefs`。

2. 挂载代码回切  
   - `components/webService/src/web_service.c`  
   - 使用 `esp_littlefs.h`；  
   - `esp_vfs_littlefs_register` + `esp_vfs_littlefs_conf_t`。

3. 分区 subtype 回切  
   - `partitions_large.csv`  
   - `www`、`lua` 均恢复为 `littlefs`。

4. 部署脚本回切并修复路径  
   - `doc/webdeploy/deployweb.bat`  
   - 打包工具恢复为 `mklittlefs.exe`；  
   - 镜像生成命令恢复为 `mklittlefs` 参数形式；  
   - 页面源目录修正为本项目实际目录 `webPage`（原脚本有 `webpage` 大小写不一致问题）。

5. 依赖源补充  
   - `main/idf_component.yml` 新增：`joltwallet/littlefs: "*"`，用于拉取 LittleFS 组件。  
   - 注意：该组件在 CMake 中的名字是 `littlefs`，不是 `esp_littlefs`；`webService` 的 `REQUIRES` 应写 `littlefs`，头文件仍用 `esp_littlefs.h`。

### 6.3 解决结果
项目已完成从 SPIFFS 到 LittleFS 的全链路回切，代码、分区、打包烧写脚本保持一致；部署流程恢复为 LittleFS 方案。

## 序号7，httpd 任务栈溢出，2026-04-01

### 7.1 什么问题
设备连接 SoftAP 并访问网页后，串口出现：

- `***ERROR*** A stack overflow in task httpd has been detected.`

### 7.2 如何解决的
原因：`esp_http_server` 默认任务栈较小，而 `uri_wifi_scan_get` 在栈上使用了 `wifi_ap_record_t recs[20]`（体积大）和 `resp[2048]`，再叠加静态文件处理中的读缓冲，易超出默认栈。

处理：

1. 在 `web_service_start` 中增大 HTTP Server 配置：`config.stack_size = 12288`，并设置合适的 `task_priority`。  
2. `uri_wifi_scan_get` 中将扫描结果与 JSON 缓冲改为堆分配（`calloc` / `malloc`），用完后 `free`。  
3. 静态文件读取 `chunk` 从 1024 改为 512，进一步降低单次请求的栈占用。

### 7.3 解决结果
栈占用显著下降且 `httpd` 任务栈余量充足，连接热点浏览页面、调用 `/api/wifi/scan` 不应再因栈溢出复位。

## 序号8，STA可上网但SoftAP下终端无法访问外网，2026-04-01

### 8.1 什么问题
ESP32-S3 作为 `STA` 已成功连接上级热点（如手机 `HONOR300`），串口可见 `Station connected with IP`；但手机/电脑连接 ESP 的 `SoftAP` 后，**无法正常访问外网**；而 PC 直接连同一上级热点可上网。

### 8.2 原因说明
`esp-iot-bridge` 的 Wi-Fi `SoftAP` 网卡通过 `esp_netif_create_default_wifi_ap()` 创建，代码里默认只为 DHCPS 打开了 **DNS 下发**（`OFFER_DNS`），**没有像 `esp_bridge_create_netif()` 那样打开“下发默认网关（DHCP option 3 / router）”**。部分手机在未收到 router 选项时，**不会把 `192.168.4.1` 当作默认网关**，导致发往公网的流量无法走 NAT，`LWIP_IPV4_NAPT` 虽开启也起不到作用。

日志里在 STA 拿到 IP、同步 DNS 后出现 `SoftAP IP network segment has changed, deauth all station` 时，属桥接更新 DNS/网段时的踢线行为；终端重连后若仍缺默认网关，出网仍会失败。

### 8.3 如何解决的
在 `main/app_main.c` 中，于 `esp_bridge_create_all_netif()` 之后、`web_service_start()` 之前增加一次 DHCPS 配置：**`ESP_NETIF_ROUTER_SOLICITATION_ADDRESS`**（与 `bridge_common.c` 中 `esp_bridge_create_netif()` 的做法一致），确保连 `SoftAP` 的终端从 DHCP 拿到**默认网关 = ESP SoftAP 地址**（一般为 `192.168.4.1`）。

### 8.4 解决结果
重新编译烧录后，`SoftAP` 客户端应能正常经 NAT 走 `STA` 出口访问外网。若仍异常，可在终端上确认：默认网关是否为 `192.168.4.1`，DNS 是否已下发（可与上级一致）。

## 序号9，SoftAP→STA→外网转发完整调试与最终成功方案（一条汇总），2026-04-02

**现象**：`STA` 已连上级（如手机热点）并能拿 `10.x` 等地址；`SoftAP` 下终端能 DHCP 到 `192.168.4.x`，但长期无法访问公网；`menuconfig` 中 `LWIP` 已开启 **IP forwarding / NAT / NAT Port Mapping**，仍可能 ping 公网超时。

**过程与修改（按顺序，最终与当前 `main/app_main.c`、`sdkconfig` 一致）**：

1. **补齐 SoftAP DHCPS，与 `bridge_common.c` 中带 DHCPS 的网卡对齐**  
   不仅下发 **router（默认网关）**，还设置 **`OFFER_DNS`、固定池（按 SoftAP 所在 `/24` 动态算 `.2`～`.9`）**，避免仅“开一次 router”或 bridge 更新 DNS 后客户端状态不完整。  
2. **`IP_EVENT_STA_GOT_IP` 回调（注册在 `esp_bridge_create_all_netif()` 之后）**  
   在 iot-bridge 完成 **同步 DNS、`deauth` SoftAP 站** 之后再执行一次上述 **整包 DHCPS 重配**，保证踢线重连后的终端重新拿到网关与 DNS。  
3. **`esp_wifi_set_ps(WIFI_PS_NONE)`**  
   关闭 STA 侧 WiFi 省电，减轻 AP+STA 同时工作时的时序与吞吐问题。  
4. **`esp_netif_set_default_netif(ev->esp_netif)`**  
   STA 拿到 IP 后把 **默认出口** 指向上行 `WIFI_STA_DEF`，避免 lwIP 转发选错默认网卡。  
5. **`ip_napt_enable(SoftAP IPv4)` 刷新**  
   在每次 DHCPS 重配结束后对 **`WIFI_AP_DEF` 当前 IP** 再调用 **`ip_napt_enable`**，保证 NAPT 与“内网侧”地址一致。  
6. **`CONFIG_LWIP_FORCE_ROUTER_FORWARDING=y`**（`sdkconfig`）  
   强化路由器式转发行为，与双网卡 NAT 场景更匹配（若有个例异常可再在 menuconfig 中对比关闭）。  
7. **验证方式**  
   lwIP NAPT 对 **ICMP（如 `ping 8.8.8.8`）往往不可靠**，应以 **`ping 192.168.4.1`**（网关）、**浏览器** 或 **`curl` 访问 HTTPS** 为准；若仅 ping 公网失败但网页正常，属预期差异。上级为手机热点时还需留意 **AP/客户端隔离** 是否阻断下游 NAT。

**结果**：按上述烧录后，**ESP32 `SoftAP` 经 `STA` 转发外网已实测成功**。
