#pragma once

#include <stdbool.h>
#include <time.h>

typedef struct {
    bool configured;
    bool active;
    int day_number;
    int total_days;
    int days_until_start;
} ramadan_status_t;

bool ramadan_date_valid(const char *date);
bool ramadan_period_valid(const char *start_date, const char *end_date);
void ramadan_status_for_date(
    const char *start_date,
    const char *end_date,
    const struct tm *local_date,
    ramadan_status_t *status);
