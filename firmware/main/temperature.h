/**
 * @file temperature.h
 * @brief Temperature sensor driver interface
 *
 * Abstract interface for temperature sensing.
 * Concrete implementation TBD based on selected sensor.
 */

#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Temperature reading result
 */
typedef struct {
    float temperature_f;    // Temperature in Fahrenheit
    float temperature_c;    // Temperature in Celsius
    bool valid;             // True if reading is valid
} temperature_reading_t;

/**
 * @brief Initialize temperature sensor
 *
 * Configures GPIO and initializes sensor communication.
 *
 * @return true on success, false on failure
 */
bool temperature_init(void);

/**
 * @brief Read current temperature
 *
 * Performs a temperature reading from the sensor.
 *
 * @return Temperature reading structure with validity flag
 */
temperature_reading_t temperature_read(void);

/**
 * @brief Convert Celsius to Fahrenheit
 *
 * @param celsius Temperature in Celsius
 * @return Temperature in Fahrenheit
 */
float temperature_c_to_f(float celsius);

/**
 * @brief Convert Fahrenheit to Celsius
 *
 * @param fahrenheit Temperature in Fahrenheit
 * @return Temperature in Celsius
 */
float temperature_f_to_c(float fahrenheit);

/**
 * @brief Check if sensor is responding
 *
 * @return true if sensor is responding, false otherwise
 */
bool temperature_sensor_ok(void);

// GPIO pin for temperature sensor (configurable)
// TODO: Move to menuconfig
#define TEMPERATURE_SENSOR_GPIO 4

#ifdef __cplusplus
}
#endif

#endif // TEMPERATURE_H
