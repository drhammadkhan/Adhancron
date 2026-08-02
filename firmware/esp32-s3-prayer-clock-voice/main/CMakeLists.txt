set(ADHAN_BASE_MAIN "${CMAKE_CURRENT_LIST_DIR}/../../esp32-s3-prayer-clock/main")

idf_component_register(
    SRCS
        "${ADHAN_BASE_MAIN}/main.c"
        "${ADHAN_BASE_MAIN}/battery_monitor.c"
        "${ADHAN_BASE_MAIN}/battery_status.c"
        "${ADHAN_BASE_MAIN}/cast_sender.c"
        "${ADHAN_BASE_MAIN}/display_ui.c"
        "${ADHAN_BASE_MAIN}/dlna_sender.c"
        "${ADHAN_BASE_MAIN}/eid.c"
        "${ADHAN_BASE_MAIN}/firmware_update.c"
        "${ADHAN_BASE_MAIN}/prayer_times.c"
        "${ADHAN_BASE_MAIN}/ramadan.c"
        "${ADHAN_BASE_MAIN}/settings.c"
        "${ADHAN_BASE_MAIN}/web_server.c"
        "voice_commands.c"
    INCLUDE_DIRS "." "${ADHAN_BASE_MAIN}"
    EMBED_FILES "${ADHAN_BASE_MAIN}/eid_takbeer.mp3"
    EMBED_TXTFILES
        "${ADHAN_BASE_MAIN}/web/index.html"
        "${ADHAN_BASE_MAIN}/web/wifi.html"
        "${ADHAN_BASE_MAIN}/web/location.html"
        "${ADHAN_BASE_MAIN}/web/app.css"
        "${ADHAN_BASE_MAIN}/web/app.js"
    REQUIRES app_update driver esp_adc esp_app_format esp_http_client esp_https_ota esp_lcd esp_lvgl_port lvgl esp_wifi esp_event esp_netif esp_http_server esp-tls esp_timer json fatfs wear_levelling mbedtls nvs_flash sdmmc mdns esp-sr
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ADHAN_VOICE_FIRMWARE=1)
