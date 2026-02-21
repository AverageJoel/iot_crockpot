/**
 * @file spi_bus.h
 * @brief Shared SPI bus initialization
 *
 * The ST7796 display and MAX31855 thermocouple share SPI2_HOST.
 * This module initializes the bus once and is safe to call from
 * both display_driver.c and temperature.c — whichever runs first
 * performs the init; subsequent calls are no-ops.
 *
 * Bus must be initialized before adding devices via spi_bus_add_device()
 * or esp_lcd_new_panel_io_spi().
 */

#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <stdbool.h>
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// All SPI devices on this project use SPI2_HOST
#define SHARED_SPI_HOST  SPI2_HOST

/**
 * @brief Initialize the shared SPI2 bus
 *
 * Idempotent — safe to call multiple times.
 * Configures SCK/MOSI/MISO from Kconfig with DMA enabled.
 *
 * @return true on success (or already initialized)
 */
bool spi_bus_init(void);

#ifdef __cplusplus
}
#endif

#endif // SPI_BUS_H
