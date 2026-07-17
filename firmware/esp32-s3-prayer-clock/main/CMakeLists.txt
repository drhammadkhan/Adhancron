idf_component_register(
    SRCS "main.c" "battery_monitor.c" "battery_status.c" "cast_sender.c" "display_ui.c" "dlna_sender.c" "eid.c" "firmware_update.c" "prayer_times.c" "ramadan.c" "settings.c" "web_server.c"
    INCLUDE_DIRS "."
    EMBED_FILES "eid_takbeer.mp3"
    REQUIRES app_update driver esp_adc esp_app_format esp_http_client esp_https_ota esp_lcd esp_lvgl_port lvgl esp_wifi esp_event esp_netif esp_http_server esp-tls esp_timer json fatfs wear_levelling mbedtls nvs_flash sdmmc mdns
)
