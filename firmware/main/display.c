/**
 * @file display.c
 * @brief Local display interface implementation
 *
 * TODO: Implement actual display driver based on selected hardware.
 * Current implementation is a stub that logs to console.
 */

#include "display.h"
#include "crockpot.h"
#include "wifi.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "display";

// Display state
static display_type_t s_display_type = DISPLAY_TYPE_NONE;
static bool s_initialized = false;

// Message overlay
static char s_message[64] = "";
static uint32_t s_message_timeout = 0;

// Button state
static volatile bool s_button_up_pressed = false;
static volatile bool s_button_down_pressed = false;
static volatile bool s_button_select_pressed = false;

// Button interrupt handler
static void IRAM_ATTR button_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t)arg;

    if (gpio_num == BUTTON_UP_GPIO) {
        s_button_up_pressed = true;
    } else if (gpio_num == BUTTON_DOWN_GPIO) {
        s_button_down_pressed = true;
    } else if (gpio_num == BUTTON_SELECT_GPIO) {
        s_button_select_pressed = true;
    }
}

// Initialize buttons
static bool init_buttons(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_UP_GPIO) |
                        (1ULL << BUTTON_DOWN_GPIO) |
                        (1ULL << BUTTON_SELECT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  // Trigger on button press (active low)
    };

    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure button GPIOs");
        return false;
    }

    // Install GPIO ISR service
    gpio_install_isr_service(0);

    // Attach interrupt handlers
    gpio_isr_handler_add(BUTTON_UP_GPIO, button_isr_handler, (void*)BUTTON_UP_GPIO);
    gpio_isr_handler_add(BUTTON_DOWN_GPIO, button_isr_handler, (void*)BUTTON_DOWN_GPIO);
    gpio_isr_handler_add(BUTTON_SELECT_GPIO, button_isr_handler, (void*)BUTTON_SELECT_GPIO);

    ESP_LOGI(TAG, "Buttons initialized");
    return true;
}

// Process button input
static void process_buttons(void)
{
    crockpot_status_t status = crockpot_get_status();
    crockpot_state_t new_state = status.state;

    if (s_button_up_pressed) {
        s_button_up_pressed = false;
        ESP_LOGI(TAG, "UP button pressed");

        // Cycle up: OFF -> WARM -> LOW -> HIGH
        switch (status.state) {
            case CROCKPOT_OFF:  new_state = CROCKPOT_WARM; break;
            case CROCKPOT_WARM: new_state = CROCKPOT_LOW;  break;
            case CROCKPOT_LOW:  new_state = CROCKPOT_HIGH; break;
            case CROCKPOT_HIGH: new_state = CROCKPOT_HIGH; break;  // Stay at max
        }
    }

    if (s_button_down_pressed) {
        s_button_down_pressed = false;
        ESP_LOGI(TAG, "DOWN button pressed");

        // Cycle down: HIGH -> LOW -> WARM -> OFF
        switch (status.state) {
            case CROCKPOT_HIGH: new_state = CROCKPOT_LOW;  break;
            case CROCKPOT_LOW:  new_state = CROCKPOT_WARM; break;
            case CROCKPOT_WARM: new_state = CROCKPOT_OFF;  break;
            case CROCKPOT_OFF:  new_state = CROCKPOT_OFF;  break;  // Stay at off
        }
    }

    if (s_button_select_pressed) {
        s_button_select_pressed = false;
        ESP_LOGI(TAG, "SELECT button pressed");

        // Toggle between OFF and last active state
        if (status.state == CROCKPOT_OFF) {
            new_state = CROCKPOT_LOW;  // Default to LOW
        } else {
            new_state = CROCKPOT_OFF;
        }
    }

    if (new_state != status.state) {
        crockpot_set_state(new_state);
        display_show_message(crockpot_state_to_string(new_state), 1000);
    }
}

// Render display (stub - implement actual rendering)
static void render_display(void)
{
    if (s_display_type == DISPLAY_TYPE_NONE) {
        return;
    }

    crockpot_status_t status = crockpot_get_status();

    // TODO: Implement actual display rendering
    // For now, just log what would be displayed
    ESP_LOGD(TAG, "Display: %s | %.1f F | %s",
             crockpot_state_to_string(status.state),
             status.temperature_f,
             status.wifi_connected ? "WiFi" : "----");
}

bool display_init(void)
{
    ESP_LOGI(TAG, "Initializing display");

    // TODO: Detect and initialize actual display hardware
    // For now, just initialize buttons

    if (!init_buttons()) {
        ESP_LOGW(TAG, "Button initialization failed");
    }

    // TODO: I2C/SPI display detection
    // For now, assume no display connected
    s_display_type = DISPLAY_TYPE_NONE;

    s_initialized = true;
    ESP_LOGI(TAG, "Display initialized (STUB - implement actual driver)");
    return true;
}

void display_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Display task started");

    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        // Process button input
        process_buttons();

        // Check message timeout
        if (s_message_timeout > 0) {
            if (s_message_timeout <= DISPLAY_UPDATE_INTERVAL_MS) {
                s_message[0] = '\0';
                s_message_timeout = 0;
            } else {
                s_message_timeout -= DISPLAY_UPDATE_INTERVAL_MS;
            }
        }

        // Render display
        render_display();

        // Wait for next update
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS));
    }
}

void display_refresh(void)
{
    render_display();
}

void display_show_message(const char* message, uint32_t duration_ms)
{
    if (message == NULL) {
        return;
    }

    strncpy(s_message, message, sizeof(s_message) - 1);
    s_message[sizeof(s_message) - 1] = '\0';
    s_message_timeout = duration_ms;

    ESP_LOGI(TAG, "Message: %s", s_message);
}

void display_clear_message(void)
{
    s_message[0] = '\0';
    s_message_timeout = 0;
}

void display_set_brightness(uint8_t brightness)
{
    // TODO: Implement brightness control
    ESP_LOGI(TAG, "Set brightness: %d%%", brightness);
}

display_type_t display_get_type(void)
{
    return s_display_type;
}
