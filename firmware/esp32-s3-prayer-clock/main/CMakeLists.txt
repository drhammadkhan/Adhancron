idf_component_register(
    SRCS "main.c" "cast_sender.c" "display_ui.c" "prayer_times.c" "settings.c" "web_server.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_lvgl_port lvgl esp_wifi esp_event esp_netif esp_http_server esp-tls esp_timer json fatfs wear_levelling nvs_flash sdmmc mdns
)
