idf_component_register(
    SRCS "src/web_service.c"
         "src/web_pc_loopback.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_http_client littlefs esp_wifi esp_netif nvs_flash json system
)
