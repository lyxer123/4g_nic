idf_component_register(
    SRCS "router_at.c"
    INCLUDE_DIRS "include"
    REQUIRES driver nvs_flash esp_app_format freertos webService
)
