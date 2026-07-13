#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "nvs.h"

#define SETTINGS_NAMESPACE "adhan"
#define SETTINGS_VERSION 6
#define LEGACY_SETTINGS_VERSION_5 5
#define LEGACY_SETTINGS_VERSION_4 4
#define LEGACY_SETTINGS_VERSION_3 3

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char timezone[64];
    double latitude;
    double longitude;
    bool location_configured;
    int volume;
    bool enabled[5];
} legacy_settings_v3_t;

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char timezone[64];
    char location_name[64];
    double latitude;
    double longitude;
    bool location_configured;
    int volume;
    bool enabled[5];
} legacy_settings_v4_t;

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char timezone[64];
    char location_name[64];
    double latitude;
    double longitude;
    bool location_configured;
    int volume;
    bool enabled[5];
    adhan_output_t output;
    char cast_device_id[48];
    char cast_device_name[64];
} legacy_settings_v5_t;

typedef struct {
    uint32_t version;
    legacy_settings_v3_t value;
} stored_settings_v3_t;

typedef struct {
    uint32_t version;
    legacy_settings_v4_t value;
} stored_settings_v4_t;

typedef struct {
    uint32_t version;
    legacy_settings_v5_t value;
} stored_settings_v5_t;

typedef struct {
    uint32_t version;
    adhan_settings_t value;
} stored_settings_t;

void settings_defaults(adhan_settings_t *settings) {
    memset(settings, 0, sizeof(*settings));
    strcpy(settings->timezone, "GMT0BST,M3.5.0/1,M10.5.0/2");
    settings->volume = 80;
    settings->output = ADHAN_OUTPUT_ATTACHED;
    settings->automatic_updates = true;
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
    size_t length = 0;
    if (nvs_get_blob(handle, "settings", NULL, &length) != ESP_OK) {
        nvs_close(handle);
        return false;
    }
    if (length == sizeof(stored_settings_t)) {
        stored_settings_t stored = {0};
        const esp_err_t result = nvs_get_blob(handle, "settings", &stored, &length);
        nvs_close(handle);
        if (result != ESP_OK || stored.version != SETTINGS_VERSION) return false;
        *settings = stored.value;
        return true;
    }
    if (length == sizeof(stored_settings_v5_t)) {
        stored_settings_v5_t stored = {0};
        const esp_err_t result = nvs_get_blob(handle, "settings", &stored, &length);
        nvs_close(handle);
        if (result != ESP_OK || stored.version != LEGACY_SETTINGS_VERSION_5) return false;
        memcpy(settings->wifi_ssid, stored.value.wifi_ssid, sizeof(settings->wifi_ssid));
        memcpy(settings->wifi_password, stored.value.wifi_password, sizeof(settings->wifi_password));
        memcpy(settings->timezone, stored.value.timezone, sizeof(settings->timezone));
        memcpy(settings->location_name, stored.value.location_name, sizeof(settings->location_name));
        settings->latitude = stored.value.latitude;
        settings->longitude = stored.value.longitude;
        settings->location_configured = stored.value.location_configured;
        settings->volume = stored.value.volume;
        memcpy(settings->enabled, stored.value.enabled, sizeof(settings->enabled));
        settings->output = stored.value.output;
        memcpy(settings->cast_device_id, stored.value.cast_device_id, sizeof(settings->cast_device_id));
        memcpy(settings->cast_device_name, stored.value.cast_device_name, sizeof(settings->cast_device_name));
        settings_save(settings);
        return true;
    }
    if (length == sizeof(stored_settings_v4_t)) {
        stored_settings_v4_t stored = {0};
        const esp_err_t result = nvs_get_blob(handle, "settings", &stored, &length);
        nvs_close(handle);
        if (result != ESP_OK || stored.version != LEGACY_SETTINGS_VERSION_4) return false;
        memcpy(settings->wifi_ssid, stored.value.wifi_ssid, sizeof(settings->wifi_ssid));
        memcpy(settings->wifi_password, stored.value.wifi_password, sizeof(settings->wifi_password));
        memcpy(settings->timezone, stored.value.timezone, sizeof(settings->timezone));
        memcpy(settings->location_name, stored.value.location_name, sizeof(settings->location_name));
        settings->latitude = stored.value.latitude;
        settings->longitude = stored.value.longitude;
        settings->location_configured = stored.value.location_configured;
        settings->volume = stored.value.volume;
        memcpy(settings->enabled, stored.value.enabled, sizeof(settings->enabled));
        settings_save(settings);
        return true;
    }
    if (length == sizeof(stored_settings_v3_t)) {
        stored_settings_v3_t stored = {0};
        const esp_err_t result = nvs_get_blob(handle, "settings", &stored, &length);
        nvs_close(handle);
        if (result != ESP_OK || stored.version != LEGACY_SETTINGS_VERSION_3) return false;
        memcpy(settings->wifi_ssid, stored.value.wifi_ssid, sizeof(settings->wifi_ssid));
        memcpy(settings->wifi_password, stored.value.wifi_password, sizeof(settings->wifi_password));
        memcpy(settings->timezone, stored.value.timezone, sizeof(settings->timezone));
        settings->wifi_ssid[sizeof(settings->wifi_ssid) - 1] = '\0';
        settings->wifi_password[sizeof(settings->wifi_password) - 1] = '\0';
        settings->timezone[sizeof(settings->timezone) - 1] = '\0';
        settings->latitude = stored.value.latitude;
        settings->longitude = stored.value.longitude;
        settings->location_configured = stored.value.location_configured;
        settings->volume = stored.value.volume;
        memcpy(settings->enabled, stored.value.enabled, sizeof(settings->enabled));
        if (settings->location_configured) {
            snprintf(settings->location_name, sizeof(settings->location_name),
                "%.4f, %.4f", settings->latitude, settings->longitude);
        }
        settings_save(settings);
        return true;
    }
    nvs_close(handle);
    return false;
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
