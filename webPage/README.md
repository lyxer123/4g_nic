# webPage（前端页面）

本目录用于存放本项目的网页端页面资源。

## 当前已实现

- **Wi-Fi STA 设置**
  - 扫描附近 SSID：`GET /api/wifi/scan`
  - 保存/修改 STA：`POST /api/wifi`，JSON：`{ "ssid": "...", "password": "..." }`
  - 从设备读取：`GET /api/wifi`（可选；未实现时会回退到本地缓存）

## 运行方式（仅前端预览）

直接用浏览器打开 `webPage/index.html` 即可预览 UI。

- 如果设备侧接口尚未实现或不可达
  - “扫描/保存/读取”会提示失败
  - 但“保存/修改”会自动回退到 `localStorage` 以便先完成页面联调

## 后端接口返回格式建议

### `GET /api/wifi/scan`

建议：

```json
{ "aps": [ { "ssid": "MyWiFi", "rssi": -55 } ] }
```

### `GET /api/wifi`

建议：

```json
{ "ssid": "MyWiFi" }
```

> 出于安全考虑，建议不要返回明文密码；页面侧会以 `******` 形式展示。

### `POST /api/wifi`

建议成功返回：

```json
{ "status": "success" }
```

失败返回：

```json
{ "status": "error", "message": "reason" }
```

