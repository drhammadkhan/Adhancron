#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "settings.h"

typedef bool (*firmware_update_window_cb_t)(void);

typedef struct {
    char current_version[32];
    char available_version[32];
    char message[128];
    bool running;
} firmware_update_status_t;

void firmware_update_start(
    const adhan_settings_t *settings,
    const bool *wifi_connected,
    SemaphoreHandle_t playback_mutex,
    firmware_update_window_cb_t window_is_safe);
bool firmware_update_request_check(void);
void firmware_update_get_status(firmware_update_status_t *status);
