# AT指令同步完成说明

## 完成时间
2026年4月16日

## 同步内容

### 1. 固件端AT指令清单

从 `components/router_at/router_at.c` 中提取的ESP32-S3已实现的AT指令：

#### 基础指令
- `AT` - 测试AT
- `ATE0` - 关闭命令回显
- `ATE1` - 开启命令回显
- `AT+GMR` - 查询固件版本信息
- `AT+IDF` - 查询ESP-IDF版本
- `AT+CHIP` - 查询芯片信息
- `AT+MEM` - 查询Flash和PSRAM大小
- `AT+RST` - 重启设备
- `AT+CMD` - 列出所有支持的AT指令
- `AT+SYSRAM` - 查询系统RAM使用情况

#### 时间管理指令
- `AT+TIME` - 查询当前系统时间
- `AT+TIME=YYYY-MM-DD HH:MM:SS` - 设置系统时间

#### 路由器相关指令
- `AT+ROUTER` - 查询路由器UART配置信息
- `AT+MODE` - 查询当前工作模式
- `AT+MODE=<id>` - 设置工作模式
- `AT+PING` - Ping测试（设备级）

#### 4G模组相关指令（需要CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM）
- `AT+MODEMINFO` - 查询4G模组详细信息
- `AT+MODEMTIME` - 查询4G模组网络时间

#### W5500以太网相关指令
- `AT+W5500` - 检测W5500芯片是否存在
- `AT+W5500IP` - 查询W5500以太网IP信息

#### USB 4G相关指令
- `AT+USB4G` - 检测USB 4G模组
- `AT+USB4GIP` - 查询USB 4G PPP接口IP

#### 网络检测指令
- `AT+NETCHECK` - 检测网络连通性（访问百度）

### 2. PC软件端更新

#### 更新文件：`PCSoftware/nic_ble_pc/admin_pages.py`

**更新内容：**
1. 更新了AT测试页面的快捷指令按钮列表（第2122-2147行）
   - 按功能分类组织指令（基础指令、路由器相关、4G模组相关、W5500以太网、USB 4G、网络检测）
   - 添加了缺失的指令：`ATE0`、`ATE1`、`AT+RST`
   - 调整了`AT+MODE`为`AT+MODE?`（查询模式）
   - 总计26个快捷按钮

2. 更新了提示信息（第2155-2163行）
   - 明确列出所有已实现的AT指令
   - 提供了更准确的使用说明

### 3. 文档创建

#### 新建文件：`PCSoftware/AT指令说明.md`

**文档内容：**
- 完整的使用方式说明
- 按功能分类的AT指令表格（包含指令、类型、说明、示例响应）
- MODEMINFO字段详细说明
- 响应格式说明
- 注意事项
- 故障排查指南
- 相关文档链接

#### 更新文件：`readme.md`

**更新内容：**
- 在文档索引表中添加了三条新记录：
  - `PCSoftware/AT指令说明.md` - ESP32-S3路由器固件实现的AT指令集说明
  - `at指令实现.md` - AT指令架构设计与实现规划评估
  - `at指令说明.md` - 乐鑫官方ESP-AT指令集参考（中文）

## 验证方式

### 在PC软件中验证
1. 打开PCSoftware管理工具
2. 连接串口
3. 进入"AT测试"菜单
4. 查看快捷指令按钮是否包含所有新增指令
5. 点击各个快捷按钮测试指令响应

### 在固件端验证
1. 通过串口发送`AT+CMD`指令
2. 验证返回的指令列表是否包含文档中列出的所有指令
3. 逐个测试各个指令的功能

## 注意事项

1. **4G模组指令**：`AT+MODEMINFO`和`AT+MODEMTIME`仅在启用`CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM`配置时可用
2. **UART共享**：当AT与日志控制台共用同一UART时，只有以`AT`开头的完整行才会被router_at处理
3. **命令回显**：默认关闭回显，可使用`ATE1`开启
4. **重启指令**：`AT+RST`会立即重启设备，请谨慎使用

## 后续建议

1. 可以考虑在PC软件中添加AT指令历史记录功能
2. 可以添加AT指令响应解析和格式化显示功能
3. 可以添加常用AT指令组合的宏命令功能
4. 建议定期同步固件端的AT指令变更到PC软件
