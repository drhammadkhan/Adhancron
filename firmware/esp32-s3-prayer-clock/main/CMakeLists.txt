idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_wifi esp_event esp_netif fatfs nvs_flash sdmmc
)
