#pragma once

#include <stdbool.h>

typedef struct {
    bool available;
    bool charging;
    bool full;
    int millivolts;
    int percentage;
} battery_status_t;

typedef struct {
    int filtered_millivolts;
    int trend_reference_millivolts;
    int trend_samples;
    int rising_windows;
    bool charging;
} battery_estimator_t;

int battery_percentage_from_millivolts(int millivolts);
void battery_estimator_update(
    battery_estimator_t *estimator,
    int measured_millivolts,
    battery_status_t *status);
