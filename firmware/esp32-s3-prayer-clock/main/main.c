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
#include "wear_levelling.h"

#include "audio_storage.h"
#include "board_config.h"
#include "cast_sender.h"
#include "display_ui.h"
#include "prayer_times.h"
#include "settings.h"
#include "web_server.h"

static const char *TAG = "adhancron_hw";

static esp_lcd_panel_handle_t display;
static i2c_master_bus_handle_t i2c_bus;
static i2s_chan_handle_t i2s_tx;
static esp_codec_dev_handle_t audio;
static int audio_sample_rate;
static bool i2s_output_enabled;
static bool sd_mounted;
static bool storage_mounted;
static wl_handle_t storage_wl_handle = WL_INVALID_HANDLE;
static bool adhan_audio_available;
static adhan_settings_t settings;
static bool ntp_started;
static bool wifi_connected;
static bool setup_ap_started;
static bool wifi_fallback_pending;
static SemaphoreHandle_t audio_mutex;
static SemaphoreHandle_t playback_mutex;

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
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &access_point));
    ESP_LOGI(TAG, "Setup Wi-Fi ready: Adhancron Setup (password: adhancron), open http://192.168.4.1");
}

static void wifi_fallback_task(void *unused) {
    vTaskDelay(pdMS_TO_TICKS(20000));
    if (!wifi_connected) {
        ESP_LOGW(TAG, "Home Wi-Fi unavailable; enabling recovery setup network");
        start_setup_access_point();
    }
    wifi_fallback_pending = false;
    vTaskDelete(NULL);
}

static void schedule_wifi_fallback(void) {
    if (wifi_connected || setup_ap_started || wifi_fallback_pending) return;
    wifi_fallback_pending = true;
    if (xTaskCreate(wifi_fallback_task, "wifi_fallback", 3072, NULL, 2, NULL) != pdPASS) {
        wifi_fallback_pending = false;
        ESP_LOGE(TAG, "Could not start Wi-Fi recovery timer");
    }
}

static void wifi_event_handler(void *argument, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected; retrying");
        esp_wifi_connect();
        schedule_wifi_fallback();
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

static void current_device_address(char address[16]) {
    address[0] = '\0';
    if (wifi_connected) {
        esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info = {0};
        if (station != NULL && esp_netif_get_ip_info(station, &ip_info) == ESP_OK &&
                ip_info.ip.addr != 0 &&
                esp_ip4addr_ntoa(&ip_info.ip, address, 16) != NULL) {
            return;
        }
    }
    if (setup_ap_started) {
        strlcpy(address, "192.168.4.1", 16);
    }
}

static void display_task(void *unused) {
    while (true) {
        char device_address[16];
        current_device_address(device_address);
        display_ui_update(
            &settings, wifi_connected, setup_ap_started,
            storage_mounted, adhan_audio_available, device_address);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void init_display(void) {
    ESP_LOGI(TAG, "Initialising ILI9341 display");

    const size_t transfer_bytes = BOARD_LCD_WIDTH * 32 * sizeof(uint16_t);

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
        .max_transfer_sz = transfer_bytes,
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
    ESP_ERROR_CHECK(display_ui_init(io, display));
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

static void play_adhan_from_storage(void) {
    if (xSemaphoreTake(audio_mutex, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Audio playback is already active");
        return;
    }
    FILE *file = fopen(ADHAN_AUDIO_PATH, "rb");
    if (file == NULL) {
        xSemaphoreGive(audio_mutex);
        return;
    }

    const size_t input_capacity = 8192;
    uint8_t *input = heap_caps_malloc(input_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *pcm = heap_caps_malloc(1152 * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    HMP3Decoder decoder = MP3InitDecoder();
    if (input == NULL || pcm == NULL || decoder == NULL) {
        ESP_LOGE(TAG, "Not enough memory to decode %s", ADHAN_AUDIO_PATH);
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
    ESP_LOGI(TAG, "Playing %s", ADHAN_AUDIO_PATH);

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

static bool current_media_url(char *url, size_t url_size) {
    esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {0};
    char address[16];
    if (station == NULL || esp_netif_get_ip_info(station, &ip_info) != ESP_OK ||
            ip_info.ip.addr == 0 ||
            esp_ip4addr_ntoa(&ip_info.ip, address, sizeof(address)) == NULL) {
        return false;
    }
    const int written = snprintf(
        url, url_size, "http://%s/audio/adhan.mp3", address);
    return written > 0 && (size_t)written < url_size;
}

static void play_adhan(void) {
    if (xSemaphoreTake(playback_mutex, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Adhan playback is already being started");
        return;
    }
    if (settings.output == ADHAN_OUTPUT_CAST) {
        cast_device_t device;
        char media_url[96];
        char error[160] = {0};
        ESP_LOGI(TAG, "Starting Cast playback on %s", settings.cast_device_name);
        bool cast_started = false;
        if (!wifi_connected) {
            strlcpy(error, "Wi-Fi is offline", sizeof(error));
        } else if (!current_media_url(media_url, sizeof(media_url))) {
            strlcpy(error, "the device audio URL is unavailable", sizeof(error));
        } else if (!cast_sender_find(settings.cast_device_id, &device, 8000)) {
            strlcpy(error, "the saved Cast speaker did not answer discovery", sizeof(error));
        } else {
            cast_started = cast_sender_play(
                &device, media_url, settings.volume, error, sizeof(error));
        }
        if (cast_started) {
            xSemaphoreGive(playback_mutex);
            return;
        }
        ESP_LOGW(TAG, "Cast playback failed%s%s; using attached speaker",
            error[0] != '\0' ? ": " : "", error);
    }
    play_adhan_from_storage();
    xSemaphoreGive(playback_mutex);
}

static bool file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    fclose(file);
    return true;
}

static bool copy_audio_file(const char *source_path) {
    FILE *source = fopen(source_path, "rb");
    if (source == NULL) {
        ESP_LOGE(TAG, "Could not open audio import source %s", source_path);
        return false;
    }
    if (fseek(source, 0, SEEK_END) != 0) {
        fclose(source);
        return false;
    }
    const long expected_size = ftell(source);
    rewind(source);
    if (expected_size <= 0) {
        fclose(source);
        return false;
    }

    remove(ADHAN_UPLOAD_PATH);
    FILE *destination = fopen(ADHAN_UPLOAD_PATH, "wb");
    if (destination == NULL) {
        ESP_LOGE(TAG, "Could not open internal audio import destination %s", ADHAN_UPLOAD_PATH);
        fclose(source);
        return false;
    }
    uint8_t *buffer = malloc(16 * 1024);
    if (buffer == NULL) {
        fclose(destination);
        fclose(source);
        remove(ADHAN_UPLOAD_PATH);
        return false;
    }

    long copied = 0;
    bool success = true;
    while (true) {
        const size_t count = fread(buffer, 1, 16 * 1024, source);
        if (count > 0) {
            if (fwrite(buffer, 1, count, destination) != count) {
                success = false;
                break;
            }
            copied += (long)count;
        }
        if (count < 16 * 1024) {
            if (ferror(source)) success = false;
            break;
        }
    }
    free(buffer);
    if (fclose(destination) != 0) success = false;
    fclose(source);

    if (!success || copied != expected_size) {
        ESP_LOGE(TAG, "Internal audio import failed after %ld of %ld bytes", copied, expected_size);
        remove(ADHAN_UPLOAD_PATH);
        return false;
    }
    remove(ADHAN_AUDIO_PATH);
    if (rename(ADHAN_UPLOAD_PATH, ADHAN_AUDIO_PATH) != 0) {
        ESP_LOGE(TAG, "Could not install imported audio in internal storage");
        remove(ADHAN_UPLOAD_PATH);
        return false;
    }
    ESP_LOGI(TAG, "Imported %ld-byte adhan MP3 into internal storage", copied);
    return true;
}

static void mount_internal_storage(void) {
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };
    const esp_err_t result = esp_vfs_fat_spiflash_mount_rw_wl(
        ADHAN_STORAGE_BASE_PATH, "storage", &mount_config, &storage_wl_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not mount internal audio storage (%s)", esp_err_to_name(result));
        return;
    }
    storage_mounted = true;
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    if (esp_vfs_fat_info(ADHAN_STORAGE_BASE_PATH, &total_bytes, &free_bytes) == ESP_OK) {
        ESP_LOGI(TAG, "Internal audio storage ready: %llu bytes total, %llu bytes free",
            (unsigned long long)total_bytes, (unsigned long long)free_bytes);
    }
    adhan_audio_available = file_exists(ADHAN_AUDIO_PATH);
    if (adhan_audio_available) {
        ESP_LOGI(TAG, "Adhan MP3 found in internal storage");
    } else {
        ESP_LOGI(TAG, "Internal storage is ready for an adhan MP3");
    }
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
            if (storage_mounted && !adhan_audio_available) {
                ESP_LOGI(TAG, "Importing /sdcard/adhan.mp3 into internal storage");
                adhan_audio_available = copy_audio_file("/sdcard/adhan.mp3");
            } else {
                ESP_LOGI(TAG, "SD adhan MP3 retained as an optional backup");
            }
        } else {
            ESP_LOGI(TAG, "No SD adhan MP3 available for import");
        }
        return;
    }

    ESP_LOGI(TAG, "No optional microSD import source (%s)", esp_err_to_name(result));
}

static void sd_retry_task(void *unused) {
    while (!sd_mounted && !adhan_audio_available) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        probe_sd_card();
    }
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
                        play_adhan();
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
    esp_netif_create_default_wifi_sta();

    if (settings_has_wifi(&settings)) {
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
        wifi_config_t station = {0};
        strlcpy((char *)station.sta.ssid, settings.wifi_ssid, sizeof(station.sta.ssid));
        strlcpy((char *)station.sta.password, settings.wifi_password, sizeof(station.sta.password));
        station.sta.threshold.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
        ESP_LOGI(TAG, "Connecting to saved Wi-Fi network: %s", settings.wifi_ssid);
        schedule_wifi_fallback();
    } else {
        start_setup_access_point();
        ESP_ERROR_CHECK(esp_wifi_start());
    }
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("adhancron"));
    ESP_ERROR_CHECK(mdns_instance_name_set("Adhancron Prayer Clock"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
    ESP_LOGI(TAG, "Dashboard address: http://adhancron.local");
    web_server_start(&settings, &storage_mounted, &adhan_audio_available, play_adhan);
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
    playback_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(
        audio_mutex == NULL || playback_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    setenv("TZ", settings.timezone, 1);
    tzset();
    ESP_LOGI(TAG, "Adhancron prayer clock starting");
    report_psram();
    init_display();
    init_audio();
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(audio, settings.volume));
    mount_internal_storage();
    probe_sd_card();
    if (!sd_mounted && !adhan_audio_available) {
        xTaskCreate(sd_retry_task, "sd_retry", 4096, NULL, 2, NULL);
    }
    probe_wifi();
    xTaskCreate(prayer_scheduler_task, "prayer_scheduler", 8192, NULL, 4, NULL);
    xTaskCreate(display_task, "display", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Prayer scheduler started. Configure Wi-Fi and location before use.");
}
