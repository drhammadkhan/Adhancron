#include "settings.h"

#include <string.h>

#include "nvs.h"

#define SETTINGS_NAMESPACE "adhan"
#define SETTINGS_VERSION 3

typedef struct {
    uint32_t version;
    adhan_settings_t value;
} stored_settings_t;

void settings_defaults(adhan_settings_t *settings) {
    memset(settings, 0, sizeof(*settings));
    strcpy(settings->timezone, "GMT0BST,M3.5.0/1,M10.5.0/2");
    settings->volume = 80;
    for (size_t index = 0; index < 5; index++) {
        settings->enabled[index] = true;
    }
}

bool settings_load(adhan_settings_t *settings) {
    settings_defaults(settings);
    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    stored_settings_t stored = {0};
    size_t length = sizeof(stored);
    const esp_err_t result = nvs_get_blob(handle, "settings", &stored, &length);
    nvs_close(handle);
    if (result != ESP_OK || length != sizeof(stored) || stored.version != SETTINGS_VERSION) {
        return false;
    }
    *settings = stored.value;
    return true;
}

bool settings_save(const adhan_settings_t *settings) {
    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const stored_settings_t stored = {.version = SETTINGS_VERSION, .value = *settings};
    const esp_err_t result = nvs_set_blob(handle, "settings", &stored, sizeof(stored));
    const esp_err_t commit_result = result == ESP_OK ? nvs_commit(handle) : result;
    nvs_close(handle);
    return commit_result == ESP_OK;
}

bool settings_has_wifi(const adhan_settings_t *settings) {
    return settings->wifi_ssid[0] != '\0';
}

bool settings_reset(void) {
    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t result = nvs_erase_all(handle);
    const esp_err_t commit_result = result == ESP_OK ? nvs_commit(handle) : result;
    nvs_close(handle);
    return commit_result == ESP_OK;
}
