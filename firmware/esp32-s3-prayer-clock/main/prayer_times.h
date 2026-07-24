#pragma once

#include <stdbool.h>
#include <time.h>

typedef struct {
    double latitude;
    double longitude;
} prayer_calculation_config_t;

typedef struct {
    int fajr_minutes;
    int sunrise_minutes;
    int dhuhr_minutes;
    int asr_minutes;
    int maghrib_minutes;
    int isha_minutes;
} prayer_times_t;

/* All returned times are local wall-clock minutes after midnight. */
bool prayer_times_calculate(const struct tm *local_date,
                            const prayer_calculation_config_t *config,
                            prayer_times_t *times);

const char *prayer_time_name(int index);
int prayer_time_minutes(const prayer_times_t *times, int index);

/*
 * Replace the five salat times while retaining the calculated sunrise.
 * Override indexes are Fajr, Dhuhr, Asr, Maghrib, and Isha.
 */
void prayer_times_apply_overrides(
    prayer_times_t *times,
    const bool enabled[5],
    const int minutes[5]);
