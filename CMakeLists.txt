cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Optional local checkouts (when folders exist) to reduce registry downloads / offline builds.
# espressif/iot_bridge comes from managed_components via main/idf_component.yml — do not add
# ../esp-iot-bridge here or it can shadow the managed copy.
#   ../esp-protocols/components      (esp_modem for S2/S3 when not taken from registry)
#   ../esp-usb/host/class/cdc        (esp_modem_usb_dte)
#   ../esp-usb/device                 (esp_tinyusb)
#   ../esp-iot-solution/tools/cmake_utilities
set(_4g_nic_extra_roots
    "${CMAKE_CURRENT_SOURCE_DIR}/../esp-protocols/components"
    "${CMAKE_CURRENT_SOURCE_DIR}/../esp-usb/host/class/cdc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../esp-usb/device"
    "${CMAKE_CURRENT_SOURCE_DIR}/../esp-iot-solution/tools/cmake_utilities"
)
foreach(_root IN LISTS _4g_nic_extra_roots)
    if(IS_DIRECTORY "${_root}")
        list(APPEND EXTRA_COMPONENT_DIRS "${_root}")
    endif()
endforeach()

string(REGEX REPLACE ".*/\(.*\)" "\\1" CURDIR ${CMAKE_CURRENT_SOURCE_DIR})
project(${CURDIR})
