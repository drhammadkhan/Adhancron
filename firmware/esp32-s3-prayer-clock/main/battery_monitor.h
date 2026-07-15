#pragma once

#include "esp_err.h"

#include "battery_status.h"

esp_err_t battery_monitor_init(void);
void battery_monitor_sample(battery_status_t *status);
void battery_monitor_get_status(battery_status_t *status);
