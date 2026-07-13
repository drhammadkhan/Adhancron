idf_component_register(
    SRCS "main.c" "prayer_times.c" "settings.c" "web_server.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_wifi esp_event esp_netif esp_http_server fatfs nvs_flash sdmmc mdns
)
