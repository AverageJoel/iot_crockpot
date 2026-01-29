/**
 * @file temperature.c
 * @brief MAX31855 thermocouple driver implementation
 *
 * SPI driver for MAX31855 thermocouple-to-digital converter.
 * Reads K-type thermocouple temperature with 0.25C resolution.
 */

#include "temperature.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "temperature";

// SPI device handle
static spi_device_handle_t s_spi_handle = NULL;

// Sensor initialized flag
static bool s_initialized = false;

// Last fault code for diagnostics
static uint8_t s_last_fault = 0;

bool temperature_init(void)
{
    ESP_LOGI(TAG, "Initializing MAX31855 on SPI (CS=%d, CLK=%d, MISO=%d)",
             MAX31855_PIN_CS, MAX31855_PIN_CLK, MAX31855_PIN_MISO);

    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .miso_io_num = MAX31855_PIN_MISO,
        .mosi_io_num = -1,  // Not used (read-only device)
        .sclk_io_num = MAX31855_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4,
    };

    esp_err_t ret = spi_bus_initialize(MAX31855_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return false;
    }

    // Add MAX31855 device to bus
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 4000000,  // 4 MHz (MAX31855 supports up to 5 MHz)
        .mode = 0,                   // SPI Mode 0 (CPOL=0, CPHA=0)
        .spics_io_num = MAX31855_PIN_CS,
        .queue_size = 1,
        .flags = 0,
    };

    ret = spi_bus_add_device(MAX31855_SPI_HOST, &dev_cfg, &s_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        spi_bus_free(MAX31855_SPI_HOST);
        return false;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "MAX31855 thermocouple sensor initialized");

    // Perform initial read to verify communication
    temperature_reading_t reading = temperature_read();
    if (reading.valid) {
        ESP_LOGI(TAG, "Initial reading: %.1f C (%.1f F)",
                 reading.temperature_c, reading.temperature_f);
    } else {
        ESP_LOGW(TAG, "Initial reading failed - check thermocouple connection");
    }

    return true;
}

temperature_reading_t temperature_read(void)
{
    temperature_reading_t reading = {
        .temperature_f = 0.0f,
        .temperature_c = 0.0f,
        .valid = false
    };

    if (!s_initialized) {
        ESP_LOGW(TAG, "Sensor not initialized");
        return reading;
    }

    // Read 32 bits from MAX31855
    uint8_t rx_data[4] = {0};
    spi_transaction_t trans = {
        .length = 32,
        .rx_buffer = rx_data,
        .tx_buffer = NULL,
    };

    esp_err_t ret = spi_device_transmit(s_spi_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI transaction failed: %s", esp_err_to_name(ret));
        return reading;
    }

    // Assemble 32-bit value (MSB first)
    uint32_t raw = ((uint32_t)rx_data[0] << 24) |
                   ((uint32_t)rx_data[1] << 16) |
                   ((uint32_t)rx_data[2] << 8) |
                   (uint32_t)rx_data[3];

    ESP_LOGD(TAG, "Raw data: 0x%08lX", (unsigned long)raw);

    // Check fault bit (bit 16)
    if (raw & 0x00010000) {
        // Fault detected - check fault type bits (0-2)
        s_last_fault = raw & 0x07;

        if (s_last_fault & 0x01) {
            ESP_LOGE(TAG, "Thermocouple fault: Open circuit (no probe connected)");
        }
        if (s_last_fault & 0x02) {
            ESP_LOGE(TAG, "Thermocouple fault: Short to GND");
        }
        if (s_last_fault & 0x04) {
            ESP_LOGE(TAG, "Thermocouple fault: Short to VCC");
        }

        return reading;
    }

    // Extract thermocouple temperature (bits 31-18, 14-bit signed)
    // Resolution: 0.25C per LSB
    int16_t tc_raw = (raw >> 18) & 0x3FFF;
    if (tc_raw & 0x2000) {
        // Sign extend for negative temperatures
        tc_raw |= 0xC000;
    }
    float temp_c = (float)tc_raw * 0.25f;

    // Extract cold junction temperature (bits 15-4, 12-bit signed)
    // Resolution: 0.0625C per LSB
    int16_t cj_raw = (raw >> 4) & 0x0FFF;
    if (cj_raw & 0x0800) {
        // Sign extend for negative temperatures
        cj_raw |= 0xF000;
    }
    float cj_temp_c = (float)cj_raw * 0.0625f;

    ESP_LOGD(TAG, "Thermocouple: %.2f C, Cold Junction: %.2f C", temp_c, cj_temp_c);

    reading.temperature_c = temp_c;
    reading.temperature_f = temperature_c_to_f(temp_c);
    reading.valid = true;

    s_last_fault = 0;

    return reading;
}

float temperature_c_to_f(float celsius)
{
    return (celsius * 9.0f / 5.0f) + 32.0f;
}

float temperature_f_to_c(float fahrenheit)
{
    return (fahrenheit - 32.0f) * 5.0f / 9.0f;
}

bool temperature_sensor_ok(void)
{
    if (!s_initialized) {
        return false;
    }

    // Perform a read and check validity
    temperature_reading_t reading = temperature_read();
    return reading.valid;
}
