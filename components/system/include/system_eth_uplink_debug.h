#pragma once

/**
 * ETH_WAN diagnostics: DHCP recovery, or static IP when CONFIG_SYSTEM_ETH_WAN_USE_STATIC_IP.
 * Call after esp_bridge_create_all_netif().
 */
void system_eth_uplink_debug_init(void);
