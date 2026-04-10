idf_component_register(
    SRCS "ble_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES bt json webService esp_app_format freertos
)
