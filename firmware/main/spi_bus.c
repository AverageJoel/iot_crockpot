/**
 * @file spi_bus.c
 * @brief Shared SPI bus initialization
 *
 * DMA is enabled so the display driver can push pixel data efficiently.
 * The MAX31855 thermocouple uses the same bus without DMA (small transfers),
 * which is fully supported — DMA vs non-DMA is per-device, not per-bus.
 */

#include "spi_bus.h"
#include "esp_log.h"

static const char *TAG = "spi_bus";

static bool s_initialized = false;

bool spi_bus_init(void)
{
    if (s_initialized) {
        return true;
    }

    // max_transfer_sz: large enough for 80 lines of 480px RGB565 at a time
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = CONFIG_CROCKPOT_SPI_MOSI,
        .miso_io_num     = CONFIG_CROCKPOT_SPI_MISO,
        .sclk_io_num     = CONFIG_CROCKPOT_SPI_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = CONFIG_CROCKPOT_LCD_WIDTH * 80 * sizeof(uint16_t),
    };

    esp_err_t ret = spi_bus_initialize(SHARED_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return false;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "SPI2 bus initialized (SCK=%d MOSI=%d MISO=%d)",
             CONFIG_CROCKPOT_SPI_SCK, CONFIG_CROCKPOT_SPI_MOSI, CONFIG_CROCKPOT_SPI_MISO);
    return true;
}
