#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ADHAN_OUTPUT_ATTACHED = 0,
    ADHAN_OUTPUT_CAST = 1,
    ADHAN_OUTPUT_DLNA = 2,
} adhan_output_t;

typedef enum {
    ADHAN_DISPLAY_DETAILED = 0,
    ADHAN_DISPLAY_FOCUS = 1,
} adhan_display_style_t;

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
    char dlna_device_url[256];
    char dlna_device_name[64];
    bool automatic_updates;
    char ramadan_start_date[11];
    char ramadan_end_date[11];
    char eid_fitr_date[11];
    char eid_adha_date[11];
    int eid_takbeer_start_minute;
    int eid_takbeer_end_minute;
    int eid_takbeer_interval_minutes;
    adhan_display_style_t display_style;
    char device_hostname[33];
} adhan_settings_t;

void settings_defaults(adhan_settings_t *settings);
bool settings_load(adhan_settings_t *settings);
bool settings_save(const adhan_settings_t *settings);
bool settings_has_wifi(const adhan_settings_t *settings);
bool settings_normalize_hostname(char *hostname, size_t capacity);
bool settings_reset(void);
