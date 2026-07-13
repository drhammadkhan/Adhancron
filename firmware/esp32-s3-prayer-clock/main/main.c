#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mp3dec.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

#include "board_config.h"
#include "prayer_times.h"
#include "settings.h"
#include "web_server.h"

static const char *TAG = "adhancron_hw";

static esp_lcd_panel_handle_t display;
static uint16_t line_buffer[BOARD_LCD_WIDTH];
static i2c_master_bus_handle_t i2c_bus;
static i2s_chan_handle_t i2s_tx;
static esp_codec_dev_handle_t audio;
static int audio_sample_rate;
static bool i2s_output_enabled;
static bool sd_mounted;
static bool adhan_audio_available;
static adhan_settings_t settings;
static bool ntp_started;
static bool wifi_connected;
static bool setup_ap_started;
static SemaphoreHandle_t audio_mutex;

static void start_setup_access_point(void) {
    if (setup_ap_started) return;
    setup_ap_started = true;
    esp_netif_create_default_wifi_ap();
    wifi_config_t access_point = {0};
    strlcpy((char *)access_point.ap.ssid, "Adhancron Setup", sizeof(access_point.ap.ssid));
    strlcpy((char *)access_point.ap.password, "adhancron", sizeof(access_point.ap.password));
    access_point.ap.ssid_len = strlen("Adhancron Setup");
    access_point.ap.channel = 1;
    access_point.ap.max_connection = 4;
    access_point.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(settings_has_wifi(&settings) ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &access_point));
    ESP_LOGI(TAG, "Setup Wi-Fi ready: Adhancron Setup (password: adhancron), open http://192.168.4.1");
}

static void wifi_fallback_task(void *unused) {
    vTaskDelay(pdMS_TO_TICKS(20000));
    if (!wifi_connected) {
        ESP_LOGW(TAG, "Home Wi-Fi unavailable; enabling recovery setup network");
        start_setup_access_point();
    }
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *argument, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected; retrying");
        esp_wifi_connect();
    }
    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        if (!ntp_started) {
            ntp_started = true;
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            ESP_LOGI(TAG, "Wi-Fi connected; synchronising time with NTP");
        }
    }
}

static void fill_rect(int x, int y, int width, int height, uint16_t color) {
    for (int column = 0; column < width; column++) {
        line_buffer[column] = color;
    }
    for (int row = y; row < y + height; row++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(display, x, row, x + width, row + 1, line_buffer));
    }
}

static const uint8_t *glyph(char character) {
    static const uint8_t digits[10][5] = {{0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e}};
    static const uint8_t letters[26][5] = {{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}};
    static const uint8_t colon[5] = {0,0x36,0x36,0,0};
    static const uint8_t dot[5] = {0,0x60,0x60,0,0};
    static const uint8_t blank[5] = {0,0,0,0,0};
    if (character >= '0' && character <= '9') return digits[character - '0'];
    if (character >= 'A' && character <= 'Z') return letters[character - 'A'];
    if (character == ':') return colon;
    if (character == '.') return dot;
    return blank;
}

static void draw_text(int x, int y, const char *text, int scale, uint16_t color) {
    for (const char *cursor = text; *cursor; cursor++, x += 6 * scale) {
        const uint8_t *bitmap = glyph(*cursor >= 'a' && *cursor <= 'z' ? *cursor - 32 : *cursor);
        for (int column = 0; column < 5; column++) for (int row = 0; row < 7; row++)
            if (bitmap[column] & (1 << row)) fill_rect(x + column * scale, y + row * scale, scale, scale, color);
    }
}

static void format_clock_minutes(int minutes, char output[6]) { unsigned value=(unsigned)(((minutes%1440)+1440)%1440); snprintf(output, 6, "%02u:%02u", value/60, value%60); }

static void draw_prayer_clock(void) {
    const uint16_t background = 0x2A22, white = 0xFFFF, mint = 0x9FF3, gold = 0xE6FC;
    fill_rect(0, 0, BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, background);
    if (!settings_has_wifi(&settings)) { draw_text(30, 55, "SETUP", 5, gold); draw_text(48, 120, "WIFI", 5, white); draw_text(33, 205, "192.168.4.1", 2, mint); return; }
    if (!wifi_connected) { draw_text(20, 90, "CONNECTING", 3, gold); draw_text(42, 145, "WIFI", 4, white); return; }
    if (!settings.location_configured) { draw_text(26, 70, "SET LOCATION", 2, gold); draw_text(18, 120, "ADHANCRON.LOCAL", 2, white); return; }
    const time_t now = time(NULL); struct tm local_now = {0}; localtime_r(&now, &local_now);
    if (local_now.tm_year < 120) { draw_text(20, 100, "SYNCING TIME", 2, gold); return; }
    char clock[6]; snprintf(clock, sizeof(clock), "%02d:%02d", local_now.tm_hour, local_now.tm_min); draw_text(29, 18, clock, 6, white);
    prayer_times_t times; prayer_calculation_config_t config = {.latitude=settings.latitude,.longitude=settings.longitude};
    if (!prayer_times_calculate(&local_now, &config, &times)) { draw_text(22, 115, "TIMES UNAVAILABLE", 2, gold); return; }
    const int rows[] = {0,1,2,3,4,5}; const char *row_names[] = {"FAJR","SUNRISE","DHUHR","ASR","IFTAR","ISHA"};
    for (int row = 0; row < 6; row++) { char value[6]; format_clock_minutes(prayer_time_minutes(&times,rows[row]),value); draw_text(10, 78+row*30,row_names[row],2,row==4?gold:mint); draw_text(166,78+row*30,value,2,white); }
    const int prayer_indexes[] = {0,2,3,4,5}; const char *prayer_names[] = {"FAJR","DHUHR","ASR","IFTAR","ISHA"};
    const int current = local_now.tm_hour*60+local_now.tm_min; int next=0; for(int i=0;i<5;i++) if(prayer_time_minutes(&times,prayer_indexes[i])>current){next=i;break;}
    draw_text(10, 272, "NEXT", 2, gold); draw_text(80,272,prayer_names[next],2,white);
    int countdown=prayer_time_minutes(&times,prayer_indexes[next])-current; if(countdown<=0)countdown+=1440; unsigned countdown_value=(unsigned)(countdown%1441); char countdown_text[16]; snprintf(countdown_text,sizeof(countdown_text),"IN %02u:%02u",countdown_value/60,countdown_value%60); draw_text(80,296,countdown_text,1,mint);
}

static void display_task(void *unused) { while (true) { draw_prayer_clock(); vTaskDelay(pdMS_TO_TICKS(30000)); } }

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

static void init_audio(void) {
    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &i2c_bus));

    const esp_err_t codec_result = i2c_master_probe(
        i2c_bus, BOARD_ES8311_I2C_ADDRESS, 100);
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
    const i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&channel_config, &i2s_tx, NULL));
    const i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK_GPIO,
            .bclk = BOARD_I2S_BCLK_GPIO,
            .ws = BOARD_I2S_LRCK_GPIO,
            .dout = BOARD_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx, &i2s_config));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx));
    i2s_output_enabled = true;

    audio_codec_i2s_cfg_t codec_i2s_config = {.tx_handle = i2s_tx};
    audio_codec_i2c_cfg_t codec_i2c_config = {
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&codec_i2s_config);
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&codec_i2c_config);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    es8311_codec_cfg_t es8311_config = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .pa_pin = BOARD_AUDIO_ENABLE_GPIO,
        .pa_reverted = true,
        .use_mclk = true,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_config);
    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    audio = esp_codec_dev_new(&device_config);
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(audio, 80));
    ESP_LOGI(TAG, "Audio path initialised; waiting for playback");
}

static void configure_audio_output(int sample_rate) {
    if (sample_rate == audio_sample_rate) {
        return;
    }

    if (audio_sample_rate != 0) {
        ESP_ERROR_CHECK(esp_codec_dev_close(audio));
    }

    if (i2s_output_enabled) {
        ESP_ERROR_CHECK(i2s_channel_disable(i2s_tx));
        i2s_output_enabled = false;
    }
    const i2s_std_clk_config_t clock_config = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(i2s_tx, &clock_config));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx));
    i2s_output_enabled = true;

    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = sample_rate,
        .channel = 2,
        .bits_per_sample = 16,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(audio, &sample_info));
    audio_sample_rate = sample_rate;
    ESP_LOGI(TAG, "Audio output configured for %d Hz stereo", sample_rate);
}

static void play_adhan_from_sd(void) {
    if (xSemaphoreTake(audio_mutex, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Audio playback is already active");
        return;
    }
    FILE *file = fopen("/sdcard/adhan.mp3", "rb");
    if (file == NULL) {
        xSemaphoreGive(audio_mutex);
        return;
    }

    const size_t input_capacity = 8192;
    uint8_t *input = heap_caps_malloc(input_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *pcm = heap_caps_malloc(1152 * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    HMP3Decoder decoder = MP3InitDecoder();
    if (input == NULL || pcm == NULL || decoder == NULL) {
        ESP_LOGE(TAG, "Not enough memory to decode /sdcard/adhan.mp3");
        free(input);
        free(pcm);
        if (decoder != NULL) {
            MP3FreeDecoder(decoder);
        }
        fclose(file);
        xSemaphoreGive(audio_mutex);
        return;
    }

    size_t available = 0;
    uint8_t *read_ptr = input;
    bool eof = false;
    int decoded_frames = 0;
    ESP_LOGI(TAG, "Playing /sdcard/adhan.mp3");

    while (available > 0 || !eof) {
        if (available < MAINBUF_SIZE * 2 && !eof) {
            memmove(input, read_ptr, available);
            const size_t bytes_read = fread(input + available, 1, input_capacity - available, file);
            available += bytes_read;
            read_ptr = input;
            eof = bytes_read == 0;
        }

        const int sync_offset = MP3FindSyncWord(read_ptr, (int)available);
        if (sync_offset < 0) {
            if (eof) {
                break;
            }
            const size_t keep = available > 3 ? 3 : available;
            memmove(input, read_ptr + available - keep, keep);
            read_ptr = input;
            available = keep;
            continue;
        }

        read_ptr += sync_offset;
        available -= sync_offset;
        uint8_t *frame_ptr = read_ptr;
        int frame_bytes = (int)available;
        uint8_t *frame_start = frame_ptr;
        const int bytes_before_decode = frame_bytes;
        const int result = MP3Decode(decoder, &frame_ptr, &frame_bytes, pcm, 0);
        read_ptr = frame_ptr;
        available = (size_t)frame_bytes;

        if (result == ERR_MP3_NONE) {
            MP3FrameInfo frame_info;
            MP3GetLastFrameInfo(decoder, &frame_info);
            configure_audio_output(frame_info.samprate);

            if (frame_info.nChans == 1) {
                for (int sample = frame_info.outputSamps - 1; sample >= 0; sample--) {
                    pcm[sample * 2] = pcm[sample];
                    pcm[sample * 2 + 1] = pcm[sample];
                }
            }
            const size_t pcm_bytes = (size_t)frame_info.outputSamps * sizeof(int16_t) * (frame_info.nChans == 1 ? 2 : 1);
            ESP_ERROR_CHECK(esp_codec_dev_write(audio, pcm, pcm_bytes));
            decoded_frames++;
        } else if (result != ERR_MP3_MAINDATA_UNDERFLOW && result != ERR_MP3_INDATA_UNDERFLOW) {
            ESP_LOGW(TAG, "Skipping malformed MP3 data (%d)", result);
            if (frame_ptr == frame_start && frame_bytes == bytes_before_decode && available > 0) {
                read_ptr++;
                available--;
            }
        }
    }

    if (audio_sample_rate != 0) {
        ESP_ERROR_CHECK(esp_codec_dev_close(audio));
        audio_sample_rate = 0;
        i2s_output_enabled = false;
    }
    ESP_LOGI(TAG, "Adhan playback finished (%d MP3 frames)", decoded_frames);
    MP3FreeDecoder(decoder);
    free(pcm);
    free(input);
    fclose(file);
    xSemaphoreGive(audio_mutex);
}

static void probe_sd_card(void) {
    if (sd_mounted) return;
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
        sd_mounted = true;
        ESP_LOGI(TAG, "microSD mounted at /sdcard");
        sdmmc_card_print_info(stdout, card);
        FILE *adhan = fopen("/sdcard/adhan.mp3", "rb");
        if (adhan != NULL) {
            fclose(adhan);
            ESP_LOGI(TAG, "Adhan MP3 found at /sdcard/adhan.mp3");
            adhan_audio_available = true;
        } else {
            ESP_LOGI(TAG, "No adhan MP3 yet. Add a file named /sdcard/adhan.mp3");
        }
        return;
    }

    ESP_LOGW(TAG, "microSD not mounted (%s). Insert a FAT32 card for audio storage.", esp_err_to_name(result));
}

static void sd_retry_task(void *unused) {
    while (!sd_mounted) { vTaskDelay(pdMS_TO_TICKS(30000)); probe_sd_card(); }
    vTaskDelete(NULL);
}

static void prayer_scheduler_task(void *unused) {
    int last_year_day = -1;
    int last_minute = -1;
    int fired[5] = {-1, -1, -1, -1, -1};
    const int prayer_indexes[] = {0, 2, 3, 4, 5};

    while (true) {
        const time_t now = time(NULL);
        struct tm local_now = {0};
        localtime_r(&now, &local_now);
        if (local_now.tm_year < 120) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        const int minute = local_now.tm_hour * 60 + local_now.tm_min;
        if (local_now.tm_yday != last_year_day) {
            memset(fired, 0xff, sizeof(fired));
            last_year_day = local_now.tm_yday;
        }
        if (minute != last_minute) {
            prayer_times_t times;
            const prayer_calculation_config_t calculation = {
                .latitude = settings.latitude,
                .longitude = settings.longitude,
            };
            if (adhan_audio_available && settings.location_configured && prayer_times_calculate(&local_now, &calculation, &times)) {
                for (int index = 0; index < 5; index++) {
                    const int prayer_minute = prayer_time_minutes(&times, prayer_indexes[index]);
                    const int minutes_late = minute - prayer_minute;
                    if (settings.enabled[index] && fired[index] != prayer_minute && minutes_late >= 0 && minutes_late <= 2) {
                        fired[index] = prayer_minute;
                        ESP_LOGI(TAG, "Scheduled %s adhan", prayer_time_name(prayer_indexes[index]));
                        play_adhan_from_sd();
                    }
                }
            }
            last_minute = minute;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void probe_wifi(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    if (settings_has_wifi(&settings)) {
        esp_netif_create_default_wifi_sta();
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
        wifi_config_t station = {0};
        strlcpy((char *)station.sta.ssid, settings.wifi_ssid, sizeof(station.sta.ssid));
        strlcpy((char *)station.sta.password, settings.wifi_password, sizeof(station.sta.password));
        station.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
        ESP_LOGI(TAG, "Connecting to saved Wi-Fi network: %s", settings.wifi_ssid);
        xTaskCreate(wifi_fallback_task, "wifi_fallback", 3072, NULL, 2, NULL);
    } else {
        start_setup_access_point();
        ESP_ERROR_CHECK(esp_wifi_start());
    }
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("adhancron"));
    ESP_ERROR_CHECK(mdns_instance_name_set("Adhancron Prayer Clock"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
    ESP_LOGI(TAG, "Dashboard address: http://adhancron.local");
    web_server_start(&settings, &sd_mounted, &adhan_audio_available, play_adhan_from_sd);
}

void app_main(void) {
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);
    gpio_config_t recovery_button = {.pin_bit_mask=1ULL<<GPIO_NUM_0,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    ESP_ERROR_CHECK(gpio_config(&recovery_button));
    if (gpio_get_level(GPIO_NUM_0) == 0) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        if (gpio_get_level(GPIO_NUM_0) == 0 && settings_reset()) {
            ESP_LOGW(TAG, "BOOT held: saved setup cleared");
            esp_restart();
        }
    }
    settings_load(&settings);
    audio_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(audio_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    setenv("TZ", settings.timezone, 1);
    tzset();
    ESP_LOGI(TAG, "Adhancron prayer clock starting");
    report_psram();
    init_display();
    init_audio();
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(audio, settings.volume));
    probe_sd_card();
    if (!sd_mounted) xTaskCreate(sd_retry_task, "sd_retry", 4096, NULL, 2, NULL);
    probe_wifi();
    xTaskCreate(prayer_scheduler_task, "prayer_scheduler", 6144, NULL, 4, NULL);
    xTaskCreate(display_task, "display", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Prayer scheduler started. Configure Wi-Fi and location before use.");
}
