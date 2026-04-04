idf_component_register(
    SRCS
        "system_wifi_dual_connect.c"
        "system_eth_uplink_debug.c"
        "system_sta_baidu_probe.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_event esp_wifi lwip esp_eth esp_timer esp_http_client
)

