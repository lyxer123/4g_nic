# PCSoftware — 4G NIC 串口管理工具（Windows）

本目录提供在 **Windows** 上通过 **USB 串口（COM）** 管理设备的桌面程序。管理请求经固件 **PCAPI** 转发到设备内 **HTTP 服务（127.0.0.1 回环）**，与浏览器访问的 **同一套 REST JSON 接口**，无需设备出网也能完成绝大多数配置。

- **入口**：仓库根目录下进入 `PCSoftware`，执行 `python main.py`（见下文环境）。
- **蓝牙**：界面「连接设置」中蓝牙区域暂为占位（后续版本）；当前请以 **串口** 为准。
- **与 Flutter**：根目录 [`app/`](../app/) 为手机端 BLE 配网示例；本工具侧重 **串口 + Web  parity**，协议说明仍可参考 [`doc/ble_protocol.md`](../doc/ble_protocol.md) / [`doc/ble_protocolcn.md`](../doc/ble_protocolcn.md)。

---

## 功能一览

| 模块 | 说明 |
|------|------|
| **连接设置** | 选择 COM 口、波特率（默认 `115200`，与工程 `CONFIG_ESP_CONSOLE_*` 一致），连接 / 断开串口；可刷新端口列表。 |
| **状态** | **总览**：系统模式、版本、时间、内存、运行时间、在线用户、4G/接口摘要等（`GET /api/dashboard/overview`）。**用户列表**：在线 STA（`GET /api/users/online`）。 |
| **网络配置** | **工作模式**：WAN 类型与 LAN 组合、与网页一致的模式表（`GET/POST /api/mode`、`/api/network/config`）。**无线**：STA 扫描 / 保存 / 读取 / 清空，SoftAP SSID/加密/密码（`/api/wifi/*`、`/api/wifi/ap`）。**有线**：以太网上行参数（`/api/eth_wan`）。刷新、保存等与设备 API 一一对应。 |
| **APN 设置** | 蜂窝 APN 读写（`/api/network/apn`）。 |
| **系统管理** | 管理密码、系统时间/时区、升级与恢复出厂、系统日志、网络探测地址、重启与定时重启计划等（各 `/api/system/*`）。 |
| **串口信息** | 右侧深色日志：设备打印的 `I/W/E` 行、CLI 提示符、**PCAPI** 请求/响应摘要等；带简单着色、自动滚屏，约 **900** 行后丢弃最旧内容。 |
| **布局** | 中部 **可拖拽分隔条** 调整左侧表单与右侧日志宽度；窗口标题在串口连接成功后显示 **端口与波特率**。 |
| **菜单逻辑** | 仅当 **串口连接成功** 时，**状态 / 网络配置 / 系统管理** 三个顶层菜单可用；断开串口后上述菜单 **灰显**，避免误操作（**连接设置** 始终可用）。 |

---

## 使用说明

### 1. 安装依赖

需 **64 位 Python 3.10+**（推荐 3.11/3.12）。

在 **`PCSoftware`** 目录下：

**PowerShell**（注意 `Activate.ps1` 路径；若禁止脚本，可先执行  
`Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser`）：

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -U pip
pip install -r requirements.txt
```

**CMD**：

```bat
python -m venv .venv
.venv\Scripts\activate.bat
pip install -U pip
pip install -r requirements.txt
```

不激活虚拟环境时，可直接使用 `.venv\Scripts\python.exe`、`.venv\Scripts\pip.exe`。

### 2. 启动程序

```powershell
cd PCSoftware
.\.venv\Scripts\python.exe main.py
```

### 3. 操作顺序（建议）

1. 菜单 **「连接设置…」**：选择 **串口**、**波特率** → **连接串口**。成功后标题栏会显示当前 COM。
2. **状态 / 网络配置 / 系统管理** 菜单变为可用；默认会打开 **总览** 页，可按需切换子菜单项。
3. 在各页面点击 **读取 / 刷新 / 保存** 等按钮；结果与报错会出现在设备日志及（部分）消息框中，PCAPI 往返可在 **串口信息** 里查看。
4. 需要更多日志宽度时，**向左拖动** 中间分隔条，放大右侧文本区域。
5. 断开时仍在 **连接设置** 中点 **断开串口**；勿与 **`idf.py monitor`** 等同时占用同一 COM 口。

### 4. 注意事项

- 串口上与固件 **UART CLI** 共线，详细命令见仓库 [`doc/命令.md`](../doc/命令.md)；固件实现见 `components/serial_cli/`。
- 若 PCAPI 请求失败，请结合 **串口信息** 中的 `HTTP` 状态与 JSON `message` 排查（例如未连串口、超时、设备返回 4xx/5xx）。
- **不要** 在本工具已打开 COM 的同时，再用其他软件打开同一端口。

---

## 环境与可选组件

- **pyserial**：串口列表与读写（`requirements.txt` 已包含）。
- **bleak**：依赖文件中保留，供后续 BLE 功能或其它脚本使用；当前主界面 **不依赖** BLE 扫描。
- **tkinter**：Python 标准库，一般 **无需单独安装**。
- **打包 exe**：见下节「打包为 Windows exe」。

---

## 打包为 Windows exe（PyInstaller）

使用仓库内 **`4g_nic_pc.spec`** 生成 **单文件** 可执行程序（无黑色控制台窗口，纯 GUI）。

### 一键构建

在 **`PCSoftware`** 目录下双击或运行：

```bat
build_windows.bat
```

若已创建虚拟环境，脚本会优先使用 `.venv\Scripts\python.exe`；否则使用系统 `python`。  
成功后输出：**`dist\4G_NIC_Admin.exe`**（可拷贝到任意 Win10/11 64 位机器运行，无需安装 Python）。

### 手动构建

```powershell
cd PCSoftware
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt -r requirements-build.txt
python -m PyInstaller --clean -y 4g_nic_pc.spec
```

### 说明与排错

- **体积**：单文件 exe 约十几 MB 属正常（含 Python 运行时与 tkinter）。
- **杀毒软件**：部分环境可能误报 PyInstaller 打包产物，必要时加入信任或改用代码签名（需自有证书）。
- **调试**：若启动无界面或异常，可暂时将 `4g_nic_pc.spec` 中 `console=False` 改为 `console=True` 重新打包，以看到报错输出。
- **自定义**：应用名称在 `4g_nic_pc.spec` 的 `name=`；图标可设置 `icon='路径.ico'`（需自行准备 `.ico`）。

---

## 协议与文档

| 内容 | 路径 |
|------|------|
| UART 子命令与 REPL 说明 | [`doc/命令.md`](../doc/命令.md) |
| BLE GATT / JSON（与移动端的约定） | [`doc/ble_protocol.md`](../doc/ble_protocol.md)、[`doc/ble_protocolcn.md`](../doc/ble_protocolcn.md) |
| 设备 Web API 与网页 | 仓库 `webPage/`、`components/webService/` |

PC 侧请求体与浏览器 **Web 管理页** 提交格式一致；具体路径以 `nic_ble_pc/admin_pages.py` 中常量（如 `P_NET`、`P_WIFI_STA`）为准。

---

## 为什么在系统「蓝牙」里可能看不到设备？

Win11 **设置 → 蓝牙** 中的列表以**经典蓝牙配对**为主。设备若以 **BLE 从机** 仅广播连应用使用，**不一定**出现在该列表，属常见情况。若需验证广播，可用 **nRF Connect**、**Bluetooth LE Explorer** 等（见旧版文档中的工具表），或等待后续在 PC 工具中接入 BLE。

---

## 在 Cursor / VS Code 中开发

1. 安装扩展：**Python**（Microsoft）。
2. `Ctrl+Shift+P` → **Python: Select Interpreter** → 选 `PCSoftware\.venv\Scripts\python.exe`。
3. 在终端激活 `.venv` 后运行：`python main.py`。

---

## 与 `app/` 的对应关系（简要）

| 项目 | `app/`（Flutter） | `PCSoftware/`（本工具） |
|------|------------------|-------------------------|
| 主要连接 | BLE | 串口（PCAPI） |
| API 形态 | 特征值 + JSON | HTTP 回环同款 REST |
| UI | Flutter | tkinter |

---

## 常见问题

**Q：菜单灰掉点不了？**  
A：请先在 **连接设置** 里 **连接串口**。仅 **连接设置** 在未连接时也可用。

**Q：与 `idf.py monitor` 冲突？**  
A：同一 UART 只能被一个程序打开，请二选一。

**Q：Python 找不到 `serial`？**  
A：在 `PCSoftware` 下对当前解释器执行 `pip install -r requirements.txt`。
