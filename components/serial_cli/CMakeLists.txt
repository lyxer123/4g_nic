idf_component_register(
    SRCS "serial_cli.c"
    INCLUDE_DIRS "include"
    REQUIRES console driver webService esp_app_format freertos
)
