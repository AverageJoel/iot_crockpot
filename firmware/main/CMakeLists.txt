idf_component_register(
    SRCS
        "main.c"
        "wifi.c"
        "crockpot.c"
        "temperature.c"
        "relay.c"
        "telegram.c"
        "display.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_http_client
        esp_tls
        json
)
