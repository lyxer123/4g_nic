// Wi-Fi dual connection optimization:
// - keep SoftAP DHCP/NAPT/router options consistent after uplink events
// - set default route to uplink when STA/Ethernet got IPv4
// - disable STA power save for better NAT/throughput

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void system_wifi_dual_connect_init(void);

#ifdef __cplusplus
}
#endif

