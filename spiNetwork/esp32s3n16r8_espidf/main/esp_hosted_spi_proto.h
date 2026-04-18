/*
 * Wire layout must match Espressif IoT bridge SPI slave
 * (managed_components/.../wifi_dongle_adapter.h + spi_slave_api.c).
 */
#pragma once

#include <stdint.h>

struct esp_payload_header {
    uint8_t if_type : 4;
    uint8_t if_num : 4;
    uint8_t flags;
    uint16_t len;
    uint16_t offset;
    uint16_t checksum;
    uint16_t seq_num;
    uint8_t reserved2;
    union {
        uint8_t reserved3;
        uint8_t hci_pkt_type;
        uint8_t priv_pkt_type;
    };
} __attribute__((packed));

#define ESP_HOSTED_DUMMY_IF_TYPE 0x0F
