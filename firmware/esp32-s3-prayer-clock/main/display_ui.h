#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "settings.h"
#include "battery_status.h"

esp_err_t display_ui_init(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_handle_t panel);

void display_ui_update(
    const adhan_settings_t *settings,
    bool wifi_connected,
    bool setup_access_point_active,
    bool storage_mounted,
    bool adhan_audio_available,
    const char *device_address,
    const battery_status_t *battery);
