idf_component_register(
    SRCS
        "system_wifi_dual_connect.c"
        "system_eth_uplink_debug.c"
        "system_sta_baidu_probe.c"
        "system_w5500_detect.c"
        "system_usb_cat1_detect.c"
        "system_hw_presence.c"
        "system_napt_compat.c"
        "system_mode_manager.c"
        "system_bridge_runtime.c"
        "system_stability.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_event esp_wifi lwip esp_eth esp_timer esp_http_client driver usb nvs_flash iot_bridge
)

