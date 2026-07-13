#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char timezone[64];
    double latitude;
    double longitude;
    bool location_configured;
    int volume;
    bool enabled[5];
} adhan_settings_t;

void settings_defaults(adhan_settings_t *settings);
bool settings_load(adhan_settings_t *settings);
bool settings_save(const adhan_settings_t *settings);
bool settings_has_wifi(const adhan_settings_t *settings);
bool settings_reset(void);
