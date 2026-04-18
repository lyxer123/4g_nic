# Conditionally include BLE settings only when Bluetooth is enabled
if(CONFIG_BT_ENABLED)
    idf_component_register(
        SRCS "ble_settings.c"
        INCLUDE_DIRS "include"
        REQUIRES bt json webService esp_app_format freertos
    )
else()
    # Bluetooth not enabled - register empty component to satisfy dependencies
    idf_component_register()
    message(STATUS "ble_settings: Bluetooth not enabled, skipping BLE component")
endif()
