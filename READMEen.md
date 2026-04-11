# 4G NIC (ESP32) — Router / NAT / Web / Serial

| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-C5 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- | -------- |

Firmware based on **Espressif [esp-iot-bridge](https://github.com/espressif/esp-iot-bridge)** (`espressif/iot_bridge`): packet forwarding between **PPP (4G modem)** and **downstream interfaces** (SoftAP, SPI Ethernet / W5500, etc.) with **IPv4 NAPT**. This repository adds a full **HTTP web UI**, **UART management (CLI + PCAPI)**, **work-mode management**, and optional **host tools** (Python desktop app, Flutter BLE scaffold).

## Version matrix

| Item | Version / note |
|------|----------------|
| ESP-IDF | **v5.2.6** (validated for this project) |
| ESP-IoT-Bridge | **1.0.2** (`espressif/iot_bridge` via `managed_components`) |

Earlier bring-up notes may reference ESP-IDF **v5.1.x**; see [`调试.md`](调试.md) for debugging history and bridge patches.

## What’s in this repository

| Area | Description |
|------|-------------|
| **Networking** | `iot_bridge`: modem (USB CDC / UART), Wi-Fi STA/SoftAP, Ethernet (EMAC / SPI W5500, SDIO per Kconfig). NAT between PPP and LAN-facing interfaces. |
| **`components/system`** | Hardware detection, **work mode** profiles, deferred mode apply, Wi-Fi dual-connect helpers, STA probe / ETH uplink debug hooks. |
| **`components/webService`** | Embedded HTTP server: REST API aligned with `webPage/` SPA; static assets from **LittleFS**. Includes **loopback HTTP** handling for UART PCAPI. |
| **`components/serial_cli`** | UART **interactive CLI** (`help`, `ping`, `mode_get`, …) and **PCAPI** bridge: host sends HTTP-like requests over serial; device forwards to local web stack (`PCAPI_OUT …`). |
| **`components/ble_settings`** | Optional **BLE GATT + JSON** control path (see [`doc/ble_protocol.md`](doc/ble_protocol.md)). Wire `ble_settings_start()` in `app_main` if you enable it. |
| **`webPage/`** | Web dashboard sources; deploy to device per [`doc/webdeploy/`](doc/webdeploy/). |
| **`PCSoftware/`** | **Windows** Python + Tk GUI: same REST as the browser over **serial PCAPI** (see [`PCSoftware/README.md`](PCSoftware/README.md)); optional **PyInstaller** build to `.exe`. |
| **`app/`** | **Flutter** scaffold for BLE (Android-first); see [`app/README.md`](app/README.md). |

Boot order (high level): NVS → `esp_netif` / events → hardware probe → **bridge netifs** from HW → saved **work mode** apply → Wi-Fi helpers → **stability init** (optional periodic heap log) → **HTTP server** → **serial CLI**.

## Stability (analysis & firmware defaults)

### Problem analysis (what hurts “runs forever” reliability)

| Issue | Effect |
|--------|--------|
| **Watchdogs disabled** | If a task deadlocks (rare lwIP/USB interaction, lock misuse), the CPU may never recover; user sees a “frozen” router until power-cycle. |
| **Internal SRAM only (no PSRAM)** | Under **4G PPP + Wi‑Fi + NAPT**, lwIP/WiFi buffers compete for the same internal heap; raises risk of **`pppos_input_tcpip failed (-1)`** (often `ERR_MEM` / pbuf or TCPIP mbox pressure) and hard-to-reproduce stalls. |
| **Default lwIP socket limits** | A NAT router forwards many flows; **too few** sockets/TCP PCBs increases “no buffer” style failures under load. |

Carrier **billing/outage**, **bad RF**, or **USB power glitches** are outside firmware; this section addresses **software and resource** margins.

### Mitigations in this repo

| Measure | Rationale |
|---------|-----------|
| **PSRAM (ESP32-S3, Octal)** | Offloads large allocations and lets WiFi/lwIP prefer external RAM (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`), reducing internal SRAM pressure. **Only enable if your module has PSRAM** — comment out all `CONFIG_SPIRAM*` in `sdkconfig.defaults.esp32s3` otherwise, then `idf.py fullclean` and rebuild. |
| **Interrupt WDT + Task WDT** | If the system hangs in a tight loop or deadlock, the chip **resets** instead of staying dead. Task timeout is ~**15s**; interrupt WDT ~**1000ms**. If USB host/PPP falsely triggers, increase timeouts in `menuconfig`. |
| **Higher lwIP limits** | `sdkconfig.defaults` raises **max sockets** and **active TCP** counts for NAT-heavy use; existing **TCPIP recv mbox** / stack sizes target PPP + Wi‑Fi (see also [`调试.md`](调试.md)). |
| **Periodic heap log** (`system_stability`) | Default every **120s** at INFO: current free heap, **minimum-ever** internal free, and PSRAM free if enabled. Helps spot **slow leaks**. Disable via **System options → Periodic heap log interval = 0**. |

After changing `sdkconfig.defaults` or target defaults, run **`idf.py fullclean`** (or regenerate `sdkconfig`) so options actually apply.

**Windows:** Keep comments in `sdkconfig.defaults` / `sdkconfig.defaults.<chip>` **ASCII-only** (English). Non-ASCII comments can make `kconfgen` fail with `UnicodeDecodeError` when the tool reads files with the system code page.

### What firmware cannot fix

- SIM **billing**, **operator core network** faults, or **tower** issues.  
- **Poor USB cable / supply** to the modem (often shows as PPP drops or host errors — still hardware/environment).  
- **Multi-WAN** bridge options all enabled without a product-level policy — routing/DNS can be ambiguous; prefer **one primary uplink** when you need predictability ([`调试.md`](调试.md) §5).

## Hardware

**Typical**

- ESP32-S3 (or other target as in the table) dev board  
- **4G modem** on **USB** (CDC ACM) and/or UART per `menuconfig`  
- Power supply adequate for modem peak current  

**Optional**

- **W5500** (SPI Ethernet) for wired LAN downstream  
- Ethernet cable / USB cables / dupont wiring per your schematic  

SPI **data-forwarding** netif and SDIO **cannot** be enabled at the same time in bridge Kconfig; **W5500 uses SPI** — enable SPI forwarding, not SDIO.

## Configuration (`menuconfig`)

- **Modem**: `Component config → Bridge Configuration → Modem Configuration` (UART vs USB, APN, etc.).  
- **Downstream / LAN**: `… → The interface used to provide network data forwarding for other devices` (SoftAP, SPI, SDIO, USB RNDIS, …).  
- **External WAN** (STA / modem / ETH WAN): separate bridge options — prefer **one clear uplink** for stable routing/DNS (see [`调试.md`](调试.md) § multi-WAN behavior).  

Project defaults and lwIP tweaks (PPP, NAPT, TCPIP mbox, etc.) are in [`sdkconfig.defaults`](sdkconfig.defaults). After changing defaults, run `idf.py fullclean` or sync `sdkconfig` as needed.

## Partitioning

- Default **4 MB** flash: [`partitions_4mb.csv`](partitions_4mb.csv).  
- Larger boards may use [`partitions_large.csv`](partitions_large.csv).

## Build, flash, monitor

```bash
idf.py set-target esp32s3   # or your chip
idf.py build flash monitor
```

Faster iterative flashes:

```bash
idf.py app-flash monitor
```

Exit monitor: **Ctrl+]**.

Upstream bridge user guide: [User_Guide.md](https://github.com/espressif/esp-iot-bridge/blob/master/components/iot_bridge/User_Guide.md) (also packaged under `managed_components/espressif__iot_bridge/`).

## Documentation index

| Doc | Content |
|-----|---------|
| [`readmecn.md`](readmecn.md) | Chinese overview (this repo’s scope). |
| [`调试.md`](调试.md) | Deep-dive: W5500 PHY reset, PPP/lwIP (`pppos_input_tcpip`), SoftAP deauth, multi-WAN limits. |
| [`doc/命令.md`](doc/命令.md) | UART CLI + BLE JSON overview; links to `serial_cli` / `ble_settings`. |
| [`doc/ble_protocol.md`](doc/ble_protocol.md) / [`doc/ble_protocolcn.md`](doc/ble_protocolcn.md) | BLE GATT UUIDs and JSON commands. |
| [`PCSoftware/README.md`](PCSoftware/README.md) | PC serial admin tool + **Windows exe** build. |
| [`app/README.md`](app/README.md) | Flutter BLE app setup. |

## License

SPDX headers in source files apply (e.g. Apache-2.0 where noted by Espressif examples).
