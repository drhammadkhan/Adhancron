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
#include "esp_timer.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "voice_commands.h"

#define VOICE_COMMAND_NEXT_PRAYER 1
#define VOICE_COMMAND_PLAY_ADHAN 2
#define VOICE_DETECTION_THRESHOLD 0.50f

static const char *TAG = "adhan_voice";

static const esp_afe_sr_iface_t *afe_handle;
static esp_afe_sr_data_t *afe_data;
static adhan_voice_commands_config_t voice_config;
static volatile bool voice_running;
static volatile bool voice_pause_requested;
static volatile bool voice_feed_paused;

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

    int64_t next_level_log = esp_timer_get_time() + 2000000;

    while (voice_running) {
        if (voice_pause_requested) {
            voice_feed_paused = true;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        voice_feed_paused = false;
        size_t bytes_read = 0;
        esp_err_t result = i2s_channel_read(
            voice_config.rx_channel, raw, stereo_bytes, &bytes_read,
            pdMS_TO_TICKS(100));
        if (voice_pause_requested) {
            continue;
        }
        if (result != ESP_OK || bytes_read == 0) {
            if (result != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Voice mic read failed: %s", esp_err_to_name(result));
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int32_t left_peak = 0;
        int32_t right_peak = 0;
        if (bytes_read >= stereo_bytes) {
            for (int index = 0; index < feed_chunksize; index++) {
                int32_t left = raw[index * 2];
                int32_t right = raw[index * 2 + 1];
                left = left < 0 ? -left : left;
                right = right < 0 ? -right : right;
                if (left > left_peak) {
                    left_peak = left;
                }
                if (right > right_peak) {
                    right_peak = right;
                }
            }
            const int active_slot = right_peak > left_peak ? 1 : 0;
            for (int index = 0; index < feed_chunksize; index++) {
                feed[index * feed_channels] = raw[index * 2 + active_slot];
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
        const int64_t now = esp_timer_get_time();
        if (now >= next_level_log) {
            ESP_LOGI(TAG, "Microphone level: left=%ld right=%ld",
                (long)left_peak, (long)right_peak);
            next_level_log = now + 2000000;
        }
        afe_handle->feed(afe_data, feed);
    }

    free(feed);
    free(raw);
    vTaskDelete(NULL);
}

static void detect_task(void *argument) {
    srmodel_list_t *models = argument;
    esp_mn_iface_t *multinet = NULL;
    model_iface_data_t *model_data = NULL;
    bool commands_allocated = false;
    char *model_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (model_name == NULL) {
        ESP_LOGE(TAG, "No English MultiNet model found in model partition");
        goto cleanup;
    }

    multinet = esp_mn_handle_from_name(model_name);
    if (multinet == NULL) {
        ESP_LOGE(TAG, "Could not create MultiNet handle for %s", model_name);
        goto cleanup;
    }
    model_data = multinet->create(model_name, 6000);
    if (model_data == NULL) {
        ESP_LOGE(TAG, "Could not load MultiNet model %s", model_name);
        goto cleanup;
    }

    // Keep the model handle returned by create(). switch_loader_mode() may
    // replace that handle; the previous implementation discarded its return
    // value and then passed a stale pointer into set_speech_commands().
    esp_err_t command_result = esp_mn_commands_alloc(multinet, model_data);
    if (command_result != ESP_OK) {
        ESP_LOGE(TAG, "Could not allocate voice command list: %s",
            esp_err_to_name(command_result));
        goto cleanup;
    }
    commands_allocated = true;
    command_result = esp_mn_commands_clear();
    if (command_result == ESP_OK) {
        command_result = esp_mn_commands_add(
            VOICE_COMMAND_NEXT_PRAYER, "NEXT PRAYER");
    }
    if (command_result == ESP_OK) {
        command_result = esp_mn_commands_add(
            VOICE_COMMAND_PLAY_ADHAN, "PLAY ADHAN");
    }
    if (command_result == ESP_OK) {
        command_result = esp_mn_commands_add(
            VOICE_COMMAND_PLAY_ADHAN, "PLAY AZAN");
    }
    if (command_result == ESP_OK) {
        command_result = esp_mn_commands_add(
            VOICE_COMMAND_PLAY_ADHAN, "PLAY ATHAN");
    }
    if (command_result == ESP_OK) {
        command_result = esp_mn_commands_add(
            VOICE_COMMAND_PLAY_ADHAN, "PLAY PRAYER CALL");
    }
    if (command_result == ESP_OK) {
        command_result = esp_mn_commands_add(
            VOICE_COMMAND_PLAY_ADHAN, "PLAY AUDIO");
    }
    if (command_result == ESP_OK) {
        command_result = esp_mn_commands_add(
            VOICE_COMMAND_PLAY_ADHAN, "START PRAYER CALL");
    }
    if (command_result != ESP_OK) {
        ESP_LOGE(TAG, "Could not configure voice commands: %s",
            esp_err_to_name(command_result));
        goto cleanup;
    }
    esp_mn_error_t *errors = esp_mn_commands_update();
    if (errors != NULL) {
        ESP_LOGE(TAG, "MultiNet rejected one or more voice commands");
        goto cleanup;
    }
    multinet->set_det_threshold(model_data, VOICE_DETECTION_THRESHOLD);
    const int mn_chunksize = multinet->get_samp_chunksize(model_data);
    const int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    if (mn_chunksize != afe_chunksize) {
        ESP_LOGE(TAG, "Voice frame mismatch: MultiNet=%d, AFE=%d",
            mn_chunksize, afe_chunksize);
        goto cleanup;
    }
    ESP_LOGI(TAG,
        "Voice command recogniser ready: model=%s, mn=%d, afe=%d, threshold=%.2f",
        model_name, mn_chunksize, afe_chunksize,
        (double)VOICE_DETECTION_THRESHOLD);

    while (voice_running) {
        if (voice_pause_requested) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
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

cleanup:
    voice_running = false;
    if (model_data != NULL && multinet != NULL) {
        multinet->destroy(model_data);
    }
    if (commands_allocated) {
        esp_mn_commands_free();
    }
    ESP_LOGW(TAG, "Voice command recogniser stopped; the prayer clock will continue normally");
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

    ESP_LOGI(TAG, "Voice memory before tasks: internal=%u, PSRAM=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    voice_running = true;
    voice_pause_requested = false;
    voice_feed_paused = false;
    if (xTaskCreatePinnedToCore(feed_task, "voice_feed", 8192, NULL, 5, NULL, 0) != pdPASS ||
            xTaskCreatePinnedToCore(detect_task, "voice_detect", 8192, models, 5, NULL, 1) != pdPASS) {
        voice_running = false;
        ESP_LOGE(TAG, "Could not start voice command tasks");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Offline voice commands enabled: NEXT PRAYER, PLAY ADHAN");
    return ESP_OK;
}

esp_err_t adhan_voice_commands_pause(void) {
    if (!voice_running || voice_config.rx_channel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    voice_pause_requested = true;
    for (int attempt = 0; attempt < 50 && !voice_feed_paused; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!voice_feed_paused) {
        voice_pause_requested = false;
        ESP_LOGE(TAG, "Microphone did not pause before playback");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Microphone paused for playback");
    return ESP_OK;
}

esp_err_t adhan_voice_commands_resume(void) {
    if (!voice_running || voice_config.rx_channel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = i2s_channel_enable(voice_config.rx_channel);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Could not restore microphone input: %s",
            esp_err_to_name(result));
        return result;
    }
    voice_feed_paused = false;
    voice_pause_requested = false;
    ESP_LOGI(TAG, "Microphone listening resumed");
    return ESP_OK;
}
