/**
 * @file relay.c
 * @brief Relay/SSR control implementation
 */

#include "relay.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "relay";

// Relay GPIO mapping
static const gpio_num_t s_relay_gpio[RELAY_CHANNEL_COUNT] = {
    [RELAY_CHANNEL_MAIN] = RELAY_MAIN_GPIO
};

// Current relay states
static bool s_relay_states[RELAY_CHANNEL_COUNT] = { false };

// Initialized flag
static bool s_initialized = false;

bool relay_init(void)
{
    ESP_LOGI(TAG, "Initializing relay control");

    // Configure all relay GPIOs
    for (int i = 0; i < RELAY_CHANNEL_COUNT; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << s_relay_gpio[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };

        if (gpio_config(&io_conf) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure GPIO for relay %d", i);
            return false;
        }

        // Initialize to OFF state
        int level = RELAY_ACTIVE_HIGH ? 0 : 1;
        gpio_set_level(s_relay_gpio[i], level);
        s_relay_states[i] = false;

        ESP_LOGI(TAG, "Relay %d configured on GPIO %d", i, s_relay_gpio[i]);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Relay control initialized");
    return true;
}

bool relay_set(relay_channel_t channel, bool on)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Relay not initialized");
        return false;
    }

    if (channel >= RELAY_CHANNEL_COUNT) {
        ESP_LOGE(TAG, "Invalid relay channel: %d", channel);
        return false;
    }

    int level;
    if (RELAY_ACTIVE_HIGH) {
        level = on ? 1 : 0;
    } else {
        level = on ? 0 : 1;
    }

    gpio_set_level(s_relay_gpio[channel], level);
    s_relay_states[channel] = on;

    ESP_LOGI(TAG, "Relay %d set to %s", channel, on ? "ON" : "OFF");
    return true;
}

bool relay_get(relay_channel_t channel)
{
    if (channel >= RELAY_CHANNEL_COUNT) {
        return false;
    }
    return s_relay_states[channel];
}

void relay_all_off(void)
{
    ESP_LOGI(TAG, "Turning all relays OFF");

    for (int i = 0; i < RELAY_CHANNEL_COUNT; i++) {
        int level = RELAY_ACTIVE_HIGH ? 0 : 1;
        gpio_set_level(s_relay_gpio[i], level);
        s_relay_states[i] = false;
    }
}

bool relay_apply_state(crockpot_state_t state)
{
    ESP_LOGI(TAG, "Applying crockpot state: %s", crockpot_state_to_string(state));

    switch (state) {
        case CROCKPOT_OFF:
            return relay_set(RELAY_CHANNEL_MAIN, false);

        case CROCKPOT_WARM:
        case CROCKPOT_LOW:
        case CROCKPOT_HIGH:
            // For simple on/off relay, all heating states turn relay on
            // TODO: Implement PWM or multi-relay for different heat levels
            return relay_set(RELAY_CHANNEL_MAIN, true);

        default:
            ESP_LOGE(TAG, "Unknown state: %d", state);
            relay_all_off();
            return false;
    }
}
