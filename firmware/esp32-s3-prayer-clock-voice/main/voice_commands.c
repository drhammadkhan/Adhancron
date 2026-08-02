#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "voice_commands.h"

#define VOICE_COMMAND_NEXT_PRAYER 1
#define VOICE_COMMAND_PLAY_ADHAN 2

static const char *TAG = "adhan_voice";

static const esp_afe_sr_iface_t *afe_handle;
static esp_afe_sr_data_t *afe_data;
static adhan_voice_commands_config_t voice_config;
static volatile bool voice_running;

static void dispatch_voice_command(int command_id) {
    switch (command_id) {
    case VOICE_COMMAND_NEXT_PRAYER:
        ESP_LOGI(TAG, "Recognised voice command: next prayer");
        if (voice_config.show_next_prayer != NULL) {
            voice_config.show_next_prayer(voice_config.context);
        }
        break;
    case VOICE_COMMAND_PLAY_ADHAN:
        ESP_LOGI(TAG, "Recognised voice command: play adhan");
        if (voice_config.play_adhan != NULL) {
            voice_config.play_adhan(voice_config.context);
        }
        break;
    default:
        ESP_LOGW(TAG, "Ignoring unknown voice command id %d", command_id);
        break;
    }
}

static void feed_task(void *argument) {
    (void)argument;
    const int feed_chunksize = afe_handle->get_feed_chunksize(afe_data);
    const int feed_channels = afe_handle->get_feed_channel_num(afe_data);
    const size_t mono_bytes = (size_t)feed_chunksize * sizeof(int16_t);
    const size_t stereo_bytes = mono_bytes * 2;

    int16_t *raw = heap_caps_malloc(stereo_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *feed = heap_caps_malloc((size_t)feed_chunksize * feed_channels * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (raw == NULL || feed == NULL) {
        ESP_LOGE(TAG, "Voice feed could not allocate audio buffers");
        free(raw);
        free(feed);
        vTaskDelete(NULL);
    }

    while (voice_running) {
        size_t bytes_read = 0;
        esp_err_t result = i2s_channel_read(
            voice_config.rx_channel, raw, stereo_bytes, &bytes_read, portMAX_DELAY);
        if (result != ESP_OK || bytes_read == 0) {
            ESP_LOGW(TAG, "Voice mic read failed: %s", esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (bytes_read >= stereo_bytes) {
            for (int index = 0; index < feed_chunksize; index++) {
                feed[index * feed_channels] = raw[index * 2];
            }
        } else {
            const size_t samples = bytes_read / sizeof(int16_t);
            const size_t copy_samples = samples < (size_t)feed_chunksize
                ? samples : (size_t)feed_chunksize;
            for (size_t index = 0; index < copy_samples; index++) {
                feed[index * feed_channels] = raw[index];
            }
            for (size_t index = copy_samples; index < (size_t)feed_chunksize; index++) {
                feed[index * feed_channels] = 0;
            }
        }

        for (int channel = 1; channel < feed_channels; channel++) {
            for (int index = 0; index < feed_chunksize; index++) {
                feed[index * feed_channels + channel] = 0;
            }
        }
        afe_handle->feed(afe_data, feed);
    }

    free(feed);
    free(raw);
    vTaskDelete(NULL);
}

static void detect_task(void *argument) {
    srmodel_list_t *models = argument;
    char *model_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (model_name == NULL) {
        ESP_LOGE(TAG, "No English MultiNet model found in model partition");
        vTaskDelete(NULL);
    }

    esp_mn_iface_t *multinet = esp_mn_handle_from_name(model_name);
    if (multinet == NULL) {
        ESP_LOGE(TAG, "Could not create MultiNet handle for %s", model_name);
        vTaskDelete(NULL);
    }
    model_iface_data_t *model_data = multinet->create(model_name, 6000);
    if (model_data == NULL) {
        ESP_LOGE(TAG, "Could not load MultiNet model %s", model_name);
        vTaskDelete(NULL);
    }
    multinet->switch_loader_mode(model_data, ESP_MN_LOAD_FROM_PSRAM_FLASH);

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mn_commands_alloc(multinet, model_data));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mn_commands_clear());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mn_commands_add(VOICE_COMMAND_NEXT_PRAYER, "NEXT PRAYER"));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mn_commands_add(VOICE_COMMAND_PLAY_ADHAN, "PLAY ADHAN"));
    esp_mn_error_t *errors = esp_mn_commands_update();
    if (errors != NULL) {
        ESP_LOGW(TAG, "One or more voice commands could not be added");
    }
    multinet->print_active_speech_commands(model_data);

    const int mn_chunksize = multinet->get_samp_chunksize(model_data);
    const int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    ESP_LOGI(TAG, "Voice command recogniser ready: model=%s, mn=%d, afe=%d",
        model_name, mn_chunksize, afe_chunksize);

    while (voice_running) {
        afe_fetch_result_t *result = afe_handle->fetch(afe_data);
        if (result == NULL || result->ret_value == ESP_FAIL) {
            ESP_LOGW(TAG, "Voice AFE fetch failed");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        esp_mn_state_t state = multinet->detect(model_data, result->data);
        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *mn_result = multinet->get_results(model_data);
            if (mn_result != NULL && mn_result->num > 0) {
                ESP_LOGI(TAG, "Voice match: command=%d phrase=%d prob=%.3f text=%s",
                    mn_result->command_id[0], mn_result->phrase_id[0],
                    (double)mn_result->prob[0], mn_result->string);
                dispatch_voice_command(mn_result->command_id[0]);
            }
            multinet->clean(model_data);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (state == ESP_MN_STATE_TIMEOUT) {
            multinet->clean(model_data);
        }
    }

    multinet->destroy(model_data);
    esp_mn_commands_free();
    vTaskDelete(NULL);
}

esp_err_t adhan_voice_commands_start(const adhan_voice_commands_config_t *config) {
    if (config == NULL || config->rx_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&voice_config, config, sizeof(voice_config));
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models == NULL) {
        ESP_LOGE(TAG, "ESP-SR model partition is unavailable");
        return ESP_FAIL;
    }

    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (afe_config == NULL) {
        ESP_LOGE(TAG, "Could not initialise voice AFE config");
        return ESP_FAIL;
    }
    afe_handle = esp_afe_handle_from_config(afe_config);
    afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);
    if (afe_data == NULL) {
        ESP_LOGE(TAG, "Could not create voice AFE");
        return ESP_FAIL;
    }

    voice_running = true;
    if (xTaskCreatePinnedToCore(feed_task, "voice_feed", 8192, NULL, 5, NULL, 0) != pdPASS ||
            xTaskCreatePinnedToCore(detect_task, "voice_detect", 8192, models, 5, NULL, 1) != pdPASS) {
        voice_running = false;
        ESP_LOGE(TAG, "Could not start voice command tasks");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Offline voice commands enabled: NEXT PRAYER, PLAY ADHAN");
    return ESP_OK;
}
