/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "system_w5500_detect.h"

#if CONFIG_SYSTEM_HW_DETECT_W5500

/* Mirror esp_eth W5500 map (see esp_eth/src/w5500.h); avoid including private IDF header. */
#define W5500_ADDR_SHIFT        16
#define W5500_BSB_SHIFT         3
#define W5500_BSB_COM_REG       0x00
#define W5500_ACCESS_MODE_READ  0
#define W5500_SPI_OP_MODE_VDM   0
#define W5500_RWB_SHIFT         2

#define W5500_MAKE_MAP(offset, bsb) (((uint32_t)(offset) << W5500_ADDR_SHIFT) | ((uint32_t)(bsb) << W5500_BSB_SHIFT))
#define W5500_REG_VERSIONR      W5500_MAKE_MAP(0x0039, W5500_BSB_COM_REG)
#define W5500_CHIP_VERSION      0x04

static const char *TAG = "w5500_det";

static bool s_present;
static uint8_t s_version_raw;

static esp_err_t w5500_spi_read_reg(spi_device_handle_t spi, uint32_t reg_map, uint8_t *out, size_t len)
{
    const uint16_t cmd = (uint16_t)(reg_map >> W5500_ADDR_SHIFT);
    const uint32_t addr_phase = (reg_map & 0xffffu) | (W5500_ACCESS_MODE_READ << W5500_RWB_SHIFT) | W5500_SPI_OP_MODE_VDM;

    spi_transaction_t t = {
        .flags = (len <= 4) ? SPI_TRANS_USE_RXDATA : 0,
        .cmd = cmd,
        .addr = addr_phase,
        .length = (uint32_t)(8 * len),
        .rx_buffer = (len > 4) ? out : NULL,
    };

    esp_err_t err = spi_device_polling_transmit(spi, &t);
    if (err != ESP_OK) {
        return err;
    }
    if ((t.flags & SPI_TRANS_USE_RXDATA) && len <= 4) {
        memcpy(out, t.rx_data, len);
    }
    return ESP_OK;
}

static void w5500_phy_reset_pulse(void)
{
#if CONFIG_BRIDGE_ETH_SPI_PHY_RST0_GPIO >= 0
    const int rst = CONFIG_BRIDGE_ETH_SPI_PHY_RST0_GPIO;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << rst,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(rst, 0);
    esp_rom_delay_us(500);
    gpio_set_level(rst, 1);
    esp_rom_delay_us(2000);
#else
    esp_rom_delay_us(1000);
#endif
}

esp_err_t system_w5500_detect_run(void)
{
    s_present = false;
    s_version_raw = 0;

    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_BRIDGE_ETH_SPI_MISO_GPIO,
        .mosi_io_num = CONFIG_BRIDGE_ETH_SPI_MOSI_GPIO,
        .sclk_io_num = CONFIG_BRIDGE_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    esp_err_t err = spi_bus_initialize(CONFIG_BRIDGE_ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI bus already in use; skip W5500 probe (run before bridge SPI init)");
        return ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    w5500_phy_reset_pulse();

    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = CONFIG_BRIDGE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_BRIDGE_ETH_SPI_CS0_GPIO,
        .queue_size = 2,
        /* Let MAC driver defaults apply if zero; we set explicitly to match W5500 framing */
        .command_bits = 16,
        .address_bits = 8,
    };

    spi_device_handle_t spi = NULL;
    err = spi_bus_add_device(CONFIG_BRIDGE_ETH_SPI_HOST, &devcfg, &spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(err));
        spi_bus_free(CONFIG_BRIDGE_ETH_SPI_HOST);
        return err;
    }

    uint8_t ver = 0;
    err = w5500_spi_read_reg(spi, W5500_REG_VERSIONR, &ver, 1);
    spi_bus_remove_device(spi);
    spi = NULL;
    spi_bus_free(CONFIG_BRIDGE_ETH_SPI_HOST);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "VERSIONR read failed: %s (no W5500 or bad SPI/RST)", esp_err_to_name(err));
        return err;
    }

    s_version_raw = ver;
    s_present = (ver == W5500_CHIP_VERSION);
    if (s_present) {
        ESP_LOGI(TAG, "W5500 detected: VERSIONR=0x%02x", ver);
    } else {
        ESP_LOGW(TAG, "Unexpected VERSIONR=0x%02x (expected 0x%02x): chip absent or wrong device", ver,
                 W5500_CHIP_VERSION);
    }
    return ESP_OK;
}

bool system_w5500_detect_present(void)
{
    return s_present;
}

uint8_t system_w5500_detect_version_raw(void)
{
    return s_version_raw;
}

#else /* !CONFIG_SYSTEM_HW_DETECT_W5500 */

esp_err_t system_w5500_detect_run(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool system_w5500_detect_present(void)
{
    return false;
}

uint8_t system_w5500_detect_version_raw(void)
{
    return 0;
}

#endif /* CONFIG_SYSTEM_HW_DETECT_W5500 */
