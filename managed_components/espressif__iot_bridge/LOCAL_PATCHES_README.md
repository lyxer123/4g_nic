# Local patches (4g_nic / Route B)

Upstream `espressif/iot_bridge` is normally managed by the ESP-IDF Component Manager.
**`idf.py update-dependencies` or re-resolving versions may overwrite files in this folder.**
After an update, re-apply patches from git history or from the project’s documented diff.

## Phase 1b: SPI dual-netif second attach must return ESP_OK

`esp_bridge_eth_spi_init()` left `ret == ESP_FAIL` when attaching **ETH_WAN** after **ETH_LAN** (driver already
started). That caused `esp_bridge_create_eth_netif()` to skip `esp_netif_up()` on ETH_WAN — DHCP toward the
PC/shared adapter never ran. Fixed by returning `ESP_OK` on the attach-only path (`eth_is_start` already true).

## Phase 1 (done here): External + Forwarding Ethernet on one PHY

**Goal:** Allow `BRIDGE_EXTERNAL_NETIF_ETHERNET` and `BRIDGE_DATA_FORWARDING_NETIF_ETHERNET`
to be enabled **at the same time** when `BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN` is **disabled**.

**Why:** Stock iot-bridge forbids this in Kconfig, but a single W5500/EMAC cannot call
`esp_bridge_eth_spi_init()` twice (one driver, two `esp_netif_attach`). The fix is to use
the same **shared MAC glue** as AUTO mode (`esp_eth_netif_glue_wrap.c`).

**Files touched:**

- `Kconfig` — remove mutual exclusion between the two Ethernet options; refresh help text.
- `CMakeLists.txt` — compile `esp_eth_netif_glue_wrap.c` when External+Forwarding pair is on.
- `src/bridge_eth.c` — `BRIDGE_ETH_USE_SHARED_MAC_GLUE` = AUTO **or** (External **and** Forwarding Ethernet).

**Still exclusive:** `BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN` vs the pair above (pick one style).

## Planned later phases (not implemented here)

- Relax / document SPI vs SDIO combined constraints (`NUM_DEFINED_MACROS`, Kconfig help).
- Optional: unify `dhcp_dns_*_customer_cb` when multiple external netifs are active (STA + PPP + ETH).
- Fork iot_bridge to a project-owned component if manager overwrites become unacceptable.
