#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

#include "board_config.h"

static const char *TAG = "adhancron_hw";

static esp_lcd_panel_handle_t display;
static uint16_t line_buffer[BOARD_LCD_WIDTH];

static void fill_rect(int x, int y, int width, int height, uint16_t color) {
    for (int column = 0; column < width; column++) {
        line_buffer[column] = color;
    }
    for (int row = y; row < y + height; row++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(display, x, row, x + width, row + 1, line_buffer));
    }
}

static void draw_hardware_screen(void) {
    // The colour bands make display orientation, colour order, and backlight
    // faults obvious before the LVGL dashboard is introduced.
    fill_rect(0, 0, BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, 0x0821);
    fill_rect(0, 0, BOARD_LCD_WIDTH, 64, 0x07E0);
    fill_rect(0, 64, BOARD_LCD_WIDTH, 64, 0x001F);
    fill_rect(0, 128, BOARD_LCD_WIDTH, 64, 0xF800);
    fill_rect(0, 192, BOARD_LCD_WIDTH, 64, 0xFFE0);
    fill_rect(0, 256, BOARD_LCD_WIDTH, 64, 0xFFFF);

    for (int x = 0; x < BOARD_LCD_WIDTH; x += 20) {
        fill_rect(x, 0, 4, BOARD_LCD_HEIGHT, 0x0000);
    }
}

static void init_display(void) {
    ESP_LOGI(TAG, "Initialising ILI9341 display");

    gpio_config_t backlight_config = {
        .pin_bit_mask = 1ULL << BOARD_LCD_BACKLIGHT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight_config));
    ESP_ERROR_CHECK(gpio_set_level(BOARD_LCD_BACKLIGHT_GPIO, 1));

    const spi_bus_config_t bus_config = {
        .sclk_io_num = BOARD_LCD_SCLK_GPIO,
        .mosi_io_num = BOARD_LCD_MOSI_GPIO,
        .miso_io_num = BOARD_LCD_MISO_GPIO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = BOARD_LCD_WIDTH * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = BOARD_LCD_CS_GPIO,
        .dc_gpio_num = BOARD_LCD_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 8,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) BOARD_LCD_HOST, &io_config, &io));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_config, &display));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(display));
    ESP_ERROR_CHECK(esp_lcd_panel_init(display));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(display, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(display, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(display, true));
    draw_hardware_screen();
}

static void report_psram(void) {
    const size_t psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM: %u bytes total, %u bytes free", (unsigned) psram_bytes, (unsigned) psram_free);
}

static void probe_i2c(void) {
    const i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, i2c_config.mode, 0, 0, 0));

    i2c_cmd_handle_t command = i2c_cmd_link_create();
    ESP_ERROR_CHECK(i2c_master_start(command));
    ESP_ERROR_CHECK(i2c_master_write_byte(
        command, (BOARD_ES8311_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true));
    ESP_ERROR_CHECK(i2c_master_stop(command));
    const esp_err_t codec_result = i2c_master_cmd_begin(I2C_NUM_0, command, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(command);
    if (codec_result == ESP_OK) {
        ESP_LOGI(TAG, "Audio codec ES8311 detected at I2C address 0x%02X", BOARD_ES8311_I2C_ADDRESS);
    } else {
        ESP_LOGW(TAG, "Audio codec not detected at 0x%02X (%s)", BOARD_ES8311_I2C_ADDRESS, esp_err_to_name(codec_result));
    }

    gpio_config_t audio_enable_config = {
        .pin_bit_mask = 1ULL << BOARD_AUDIO_ENABLE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&audio_enable_config));
    ESP_ERROR_CHECK(gpio_set_level(BOARD_AUDIO_ENABLE_GPIO, 0));
    ESP_LOGI(TAG, "Audio amplifier enabled; codec playback is the next milestone");
}

static void probe_sd_card(void) {
    sdmmc_card_t *card = NULL;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = BOARD_SD_CLK_GPIO;
    slot_config.cmd = BOARD_SD_CMD_GPIO;
    slot_config.d0 = BOARD_SD_D0_GPIO;
    slot_config.d1 = BOARD_SD_D1_GPIO;
    slot_config.d2 = BOARD_SD_D2_GPIO;
    slot_config.d3 = BOARD_SD_D3_GPIO;

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    const esp_err_t result = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "microSD mounted at /sdcard");
        sdmmc_card_print_info(stdout, card);
        return;
    }

    ESP_LOGW(TAG, "microSD not mounted (%s). Insert a FAT32 card for audio storage.", esp_err_to_name(result));
}

static void probe_wifi(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Wi-Fi scan complete: %u networks visible", ap_count);
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "Adhancron ESP32-S3 hardware check starting");
    report_psram();
    init_display();
    probe_i2c();
    probe_sd_card();
    probe_wifi();
    ESP_LOGI(TAG, "Hardware check complete. LCD colour bands should be visible.");
}
