#include "battery_monitor.h"

#include <stdbool.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_config.h"

static const char *TAG = "adhan_battery";
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t calibration_handle;
static battery_estimator_t estimator;
static battery_status_t current_status;
static SemaphoreHandle_t status_mutex;

esp_err_t battery_monitor_init(void) {
    status_mutex = xSemaphoreCreateMutex();
    if (status_mutex == NULL) return ESP_ERR_NO_MEM;

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = BOARD_BATTERY_ADC_UNIT,
    };
    esp_err_t result = adc_oneshot_new_unit(&unit_config, &adc_handle);
    if (result != ESP_OK) return result;

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    result = adc_oneshot_config_channel(
        adc_handle, BOARD_BATTERY_ADC_CHANNEL, &channel_config);
    if (result != ESP_OK) return result;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = BOARD_BATTERY_ADC_UNIT,
        .chan = BOARD_BATTERY_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    result = adc_cali_create_scheme_curve_fitting(
        &calibration_config, &calibration_handle);
    if (result != ESP_OK) {
        calibration_handle = NULL;
        ESP_LOGW(TAG, "ADC calibration unavailable: %s", esp_err_to_name(result));
    }
#endif

    ESP_LOGI(TAG, "Battery monitor ready on GPIO 9");
    return ESP_OK;
}

void battery_monitor_sample(battery_status_t *status) {
    if (status == NULL) return;
    battery_status_t sampled_status = {0};
    int raw_total = 0;
    int successful_samples = 0;
    for (int sample = 0; sample < 16; sample++) {
        int raw = 0;
        if (adc_oneshot_read(
                adc_handle, BOARD_BATTERY_ADC_CHANNEL, &raw) == ESP_OK) {
            raw_total += raw;
            successful_samples++;
        }
    }
    if (successful_samples == 0) {
        goto store_status;
    }

    const int average_raw = raw_total / successful_samples;
    int divided_millivolts = 0;
    if (calibration_handle != NULL) {
        if (adc_cali_raw_to_voltage(
                calibration_handle, average_raw, &divided_millivolts) != ESP_OK) {
            divided_millivolts = 0;
        }
    } else {
        divided_millivolts = average_raw * 3100 / 4095;
    }
    battery_estimator_update(
        &estimator,
        divided_millivolts * BOARD_BATTERY_VOLTAGE_MULTIPLIER,
        &sampled_status);

store_status:
    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        current_status = sampled_status;
        xSemaphoreGive(status_mutex);
    }
    *status = sampled_status;
}

void battery_monitor_get_status(battery_status_t *status) {
    if (status == NULL) return;
    *status = (battery_status_t){0};
    if (status_mutex != NULL &&
            xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *status = current_status;
        xSemaphoreGive(status_mutex);
    }
}
