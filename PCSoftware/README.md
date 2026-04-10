# PCSoftware — Windows 11 BLE / 串口 配置工具

与根目录 [`app/`](../app/) 类似：在本机用 **低功耗蓝牙（BLE）** 或 **串口（COM）** 连接设备（**同一时间只能使用一种方式**：连接 BLE 时会断开串口，连接串口前若已连 BLE 会先断开 BLE）。

- **BLE**：连接 **4G_NIC_CFG** 从机，通过 [`doc/ble_protocol.md`](../doc/ble_protocol.md) 的 GATT/JSON 协议读写 **work_mode** 等（`ping` / `get_mode` / `set_mode` / `version`）。左侧 **自定义 JSON** 仅在此模式下可用。
- **串口**：`115200` 等波特率（与工程 `CONFIG_ESP_CONSOLE_*` 一致），收发与 [`doc/命令.md`](../doc/命令.md) 中 UART CLI 一致（`help` / `ping` / `mode_get` / `mode_set` / `version`）。

界面 **右侧** 为深色日志区：按 ESP-IDF 风格对 `I`/`W`/`E`、`ok`/`ERR` 等行着色，自动滚动，超过约 **800 行** 会丢弃最旧行。**左侧** 为扫描/连接、串口选择与 **`doc/命令.md`** 对应的快捷按钮，减少手敲命令。

## 环境（Win11）

1. 安装 **64 位 Python 3.10+**（[python.org](https://www.python.org/downloads/) 或 `winget install Python.Python.3.12`）。
2. 打开 **设置 → 蓝牙和其他设备 → 蓝牙**，确保蓝牙已开启。
3. 若在虚拟机中，需能直通主机蓝牙；否则 BLE 可能不可用。

## 为什么在「设置 → 蓝牙」里看不到设备？

Win11 里那份列表主要是 **经典蓝牙配对**。本项目的 ESP32-S3 一般是 **BLE（低功耗蓝牙）从机**，很多情况下 **不会出现在系统设置的可添加设备列表**里，这是正常现象。请用下面 **BLE 扫描工具** 或本目录的 **`python main.py`** 扫描；目标广播名 **`4G_NIC_CFG`**（亦可能显示为带 `4G_NIC` 的名称）。

若固件曾出现 `config_adv_data failed`，需先烧录 **已修复广播数据** 的版本（见 `components/ble_settings/ble_settings.c` 里 `min_interval`/`max_interval` 为 `0xFFFF`），否则设备可能根本不对外广播。

## 可安装的 BLE 工具（Windows 11）

| 工具 | 说明 |
|------|------|
| **本仓库 PCSoftware** | `pip install -r requirements.txt` 后 `python main.py`，点「扫描」即可（依赖本机蓝牙）。 |
| **[Bluetooth LE Explorer](https://apps.microsoft.com/detail/9n0z26ndvkz9)** | 微软商店免费应用，可扫描、连接、看 GATT 与读写特征。 |
| **[nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-desktop/Download#infotabs)** | Nordic 官方，带 **Scanner**，适合开发调试（不必有 Nordic 芯片）。 |
| **手机 nRF Connect**（Android / iOS） | 应用商店搜 **nRF Connect**，扫一下可确认设备是否在广播（排除仅 PC 蓝牙问题）。安卓还可装 **BLE Scanner** 等作备选。 |

笔记本请确认 **飞行模式关闭**、蓝牙驱动无黄色感叹号；部分台式机用的是 **仅 2.4G 无线键鼠接收器**（伪装成 USB 小辫子），**没有 BLE**，需在主板或外接 **带 BLE 的 USB 蓝牙适配器**。

## 虚拟环境与依赖

在 **`PCSoftware`** 目录下创建虚拟环境并安装依赖。

**PowerShell**（注意前面的 `.\`，且脚本名为 `Activate.ps1`，不要只写 `activate`，否则会被当成模块名报错）：

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -U pip
pip install -r requirements.txt
```

若提示「无法加载，因为在此系统上禁止运行脚本」，先对当前用户放行（一次性）：

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

**CMD** 可用：

```bat
python -m venv .venv
.venv\Scripts\activate.bat
pip install -U pip
pip install -r requirements.txt
```

**不激活也可以**：直接用 `.venv\Scripts\pip.exe`、`.venv\Scripts\python.exe`。

## 在 Cursor 中开发

1. 安装扩展：**Python**（Microsoft）。
2. `Ctrl+Shift+P` → **Python: Select Interpreter** → 选择 `PCSoftware\.venv\Scripts\python.exe`。
3. 终端已激活 `.venv` 时运行：`python main.py`。

## 运行

PowerShell：

```powershell
cd PCSoftware
.\.venv\Scripts\Activate.ps1
python main.py
```

或不激活：

```powershell
.\.venv\Scripts\python.exe main.py
```

1. **BLE**：点 **扫描**，选中设备后 **连接**；通知与日志出现在 **右侧**。
2. **串口**：选 **COM**（可 **刷新** 列表）、波特率，点 **连接串口**；设备输出进入同一日志区。
3. 使用左侧 **快捷命令**（随当前连接自动发 JSON 或 CLI 行），或仅在 BLE 已连时用 **自定义 JSON**。

## 协议与网页参数

- UUID、命令表与安全说明见 **`doc/ble_protocol.md`**（中文版：`doc/ble_protocolcn.md`）。
- **串口侧子命令**（`4g_nic> ` 提示符、`mode_get` / `mode_set` 等）见 **`doc/命令.md`**；固件：`components/serial_cli/`（`esp_console` 注册命令 + `uart_read_bytes` / `esp_console_run`，**非** linenoise REPL，避免与日志重入）。
- 网页上其余 POST 字段（LAN、WAN、SoftAP 等）需在固件侧扩展 JSON `cmd` 与串口子命令；本 BLE 工具只需发送对应 JSON，**无需改 PC 侧架构**。

## 串口与 BLE 同时调试

本程序已内置串口页；若仍用 **`idf.py monitor`** 占用了同一 UART，请只开其一，避免两路同时打开同一 COM 口。

## 技术说明

- **bleak**：跨平台 BLE，在 Win11 上使用系统蓝牙栈。
- **pyserial**：串口列表与读写（`pip install -r requirements.txt` 已包含）。
- **tkinter**：Python 自带 GUI，无需额外安装 Qt。
- 可选：稍后用 **PyInstaller** 打包为 `.exe`（`pip install pyinstaller`，再对 `main.py` 打包）；当前以源码运行为主。

## 与 `app/` 的对应关系

| 项目 | `app/`（Flutter） | `PCSoftware/`（Python） |
|------|-------------------|-------------------------|
| BLE 库 | flutter_blue_plus | bleak |
| UI | Flutter | tkinter |
| 协议 | 同 `0xFF50` / `FF51` / `FF52` + JSON | 相同 |
