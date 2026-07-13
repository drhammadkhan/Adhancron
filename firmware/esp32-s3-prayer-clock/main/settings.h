#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ADHAN_OUTPUT_ATTACHED = 0,
    ADHAN_OUTPUT_CAST = 1,
} adhan_output_t;

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
    bool automatic_updates;
} adhan_settings_t;

void settings_defaults(adhan_settings_t *settings);
bool settings_load(adhan_settings_t *settings);
bool settings_save(const adhan_settings_t *settings);
bool settings_has_wifi(const adhan_settings_t *settings);
bool settings_reset(void);
