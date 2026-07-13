#include "firmware_update.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/task.h"

#define OTA_VERSION_URL "https://drhammadkhan.github.io/Adhancron/firmware/version.txt"
#define OTA_IMAGE_URL "https://drhammadkhan.github.io/Adhancron/firmware/adhan-clock-esp32-s3-ota.bin"
#define OTA_INITIAL_DELAY_MS (2 * 60 * 1000)
#define OTA_REGULAR_DELAY_MS (6 * 60 * 60 * 1000)
#define OTA_RETRY_DELAY_MS (15 * 60 * 1000)

static const char *TAG = "adhan_ota";
static const adhan_settings_t *current_settings;
static const bool *station_connected;
static SemaphoreHandle_t playback_lock;
static SemaphoreHandle_t status_lock;
static TaskHandle_t update_task_handle;
static firmware_update_window_cb_t safe_window;
static firmware_update_status_t update_status;
static bool check_queued;

typedef struct {
    char value[32];
    size_t length;
} version_response_t;

static void set_status(bool running, const char *format, ...) {
    if (status_lock == NULL || xSemaphoreTake(status_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        return;
    }
    update_status.running = running;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(update_status.message, sizeof(update_status.message), format, arguments);
    va_end(arguments);
    xSemaphoreGive(status_lock);
}

static esp_err_t version_event_handler(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0 || event->user_data == NULL) {
        return ESP_OK;
    }
    version_response_t *response = event->user_data;
    const size_t remaining = sizeof(response->value) - 1 - response->length;
    const size_t count = (size_t)event->data_len < remaining
        ? (size_t)event->data_len : remaining;
    if (count > 0) {
        memcpy(response->value + response->length, event->data, count);
        response->length += count;
        response->value[response->length] = '\0';
    }
    return ESP_OK;
}

static bool fetch_latest_version(char version[32]) {
    version_response_t response = {0};
    const esp_http_client_config_t config = {
        .url = OTA_VERSION_URL,
        .event_handler = version_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return false;
    }
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    const esp_err_t result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (result != ESP_OK || status_code != 200 || response.length == 0) {
        ESP_LOGW(TAG, "Version check failed (%s, HTTP %d)",
            esp_err_to_name(result), status_code);
        return false;
    }

    size_t start = 0;
    while (start < response.length &&
            (response.value[start] == ' ' || response.value[start] == '\r' ||
             response.value[start] == '\n' || response.value[start] == '\t')) {
        start++;
    }
    size_t end = response.length;
    while (end > start &&
            (response.value[end - 1] == ' ' || response.value[end - 1] == '\r' ||
             response.value[end - 1] == '\n' || response.value[end - 1] == '\t')) {
        end--;
    }
    const size_t length = end - start;
    if (length == 0 || length >= 32) {
        return false;
    }
    memcpy(version, response.value + start, length);
    version[length] = '\0';
    return true;
}

static bool install_update(const char *version) {
    if (safe_window != NULL && !safe_window()) {
        set_status(false, "Version %s is ready; installation is postponed around adhan time", version);
        return false;
    }
    if (xSemaphoreTake(playback_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        set_status(false, "Version %s is ready; installation is waiting for playback to finish", version);
        return false;
    }

    set_status(true, "Installing version %s", version);
    wifi_ps_type_t previous_power_save = WIFI_PS_MIN_MODEM;
    esp_wifi_get_ps(&previous_power_save);
    esp_wifi_set_ps(WIFI_PS_NONE);

    char image_url[192];
    const int image_url_length = snprintf(
        image_url, sizeof(image_url), "%s?v=%s", OTA_IMAGE_URL, version);
    if (image_url_length < 0 || (size_t)image_url_length >= sizeof(image_url)) {
        xSemaphoreGive(playback_lock);
        set_status(false, "Update URL is invalid");
        return false;
    }
    const esp_http_client_config_t http_config = {
        .url = image_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size = 4096,
        .keep_alive_enable = true,
    };
    const esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .partial_http_download = true,
        .max_http_request_size = 4096,
    };
    ESP_LOGI(TAG, "Installing firmware %s from %s", version, image_url);
    const esp_err_t result = esp_https_ota(&ota_config);
    esp_wifi_set_ps(previous_power_save);
    xSemaphoreGive(playback_lock);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Firmware installation failed: %s", esp_err_to_name(result));
        set_status(false, "Update failed: %s", esp_err_to_name(result));
        return false;
    }

    set_status(false, "Version %s installed; restarting", version);
    ESP_LOGI(TAG, "Firmware %s installed; restarting", version);
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
    return true;
}

static bool check_for_update(void) {
    char latest_version[32] = {0};
    const char *installed_version = esp_app_get_description()->version;
    set_status(true, "Checking for updates");
    if (!fetch_latest_version(latest_version)) {
        set_status(false, "Could not reach the update service; will retry later");
        return false;
    }
    if (status_lock != NULL && xSemaphoreTake(status_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        strlcpy(update_status.available_version, latest_version,
            sizeof(update_status.available_version));
        xSemaphoreGive(status_lock);
    }
    if (strcmp(latest_version, installed_version) == 0) {
        set_status(false, "Firmware is up to date");
        return true;
    }
    return install_update(latest_version);
}

static void update_manager_task(void *unused) {
    TickType_t wait_time = pdMS_TO_TICKS(OTA_INITIAL_DELAY_MS);
    while (true) {
        const bool manual = ulTaskNotifyTake(pdTRUE, wait_time) > 0;
        if (status_lock != NULL && xSemaphoreTake(status_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
            check_queued = false;
            xSemaphoreGive(status_lock);
        }
        if (!manual && !current_settings->automatic_updates) {
            set_status(false, "Automatic updates are off");
            wait_time = pdMS_TO_TICKS(OTA_REGULAR_DELAY_MS);
            continue;
        }
        if (station_connected == NULL || !*station_connected) {
            set_status(false, "Waiting for an internet connection");
            wait_time = pdMS_TO_TICKS(OTA_RETRY_DELAY_MS);
            continue;
        }
        if (!manual && !current_settings->location_configured) {
            set_status(false, "Automatic updates start after setup is complete");
            wait_time = pdMS_TO_TICKS(OTA_RETRY_DELAY_MS);
            continue;
        }
        const bool completed = check_for_update();
        wait_time = pdMS_TO_TICKS(completed ? OTA_REGULAR_DELAY_MS : OTA_RETRY_DELAY_MS);
    }
}

void firmware_update_start(
        const adhan_settings_t *settings,
        const bool *wifi_connected,
        SemaphoreHandle_t playback_mutex,
        firmware_update_window_cb_t window_is_safe) {
    current_settings = settings;
    station_connected = wifi_connected;
    playback_lock = playback_mutex;
    safe_window = window_is_safe;
    status_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(status_lock == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    strlcpy(update_status.current_version, esp_app_get_description()->version,
        sizeof(update_status.current_version));
    strlcpy(update_status.message, "Update check scheduled", sizeof(update_status.message));

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
            state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
        ESP_LOGI(TAG, "Confirmed firmware %s after successful startup",
            update_status.current_version);
    }

    ESP_ERROR_CHECK(xTaskCreate(
        update_manager_task, "firmware_update", 12288, NULL, 2,
        &update_task_handle) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

bool firmware_update_request_check(void) {
    if (update_task_handle == NULL || status_lock == NULL) {
        return false;
    }
    if (xSemaphoreTake(status_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        return false;
    }
    if (update_status.running || check_queued) {
        xSemaphoreGive(status_lock);
        return false;
    }
    check_queued = true;
    strlcpy(update_status.message, "Update check queued", sizeof(update_status.message));
    xSemaphoreGive(status_lock);
    xTaskNotifyGive(update_task_handle);
    return true;
}

void firmware_update_get_status(firmware_update_status_t *status) {
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    if (status_lock != NULL && xSemaphoreTake(status_lock, pdMS_TO_TICKS(250)) == pdTRUE) {
        *status = update_status;
        xSemaphoreGive(status_lock);
    }
}
