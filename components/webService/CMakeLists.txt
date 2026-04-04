idf_component_register(
    SRCS "src/web_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server littlefs esp_wifi esp_netif nvs_flash json
)
