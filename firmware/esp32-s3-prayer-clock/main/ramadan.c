#include "ramadan.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static bool parse_date(const char *date, int *year, int *month, int *day) {
    if (date == NULL || strlen(date) != 10) {
        return false;
    }
    char trailing = '\0';
    if (sscanf(date, "%4d-%2d-%2d%c", year, month, day, &trailing) != 3 ||
            *year < 2000 || *year > 2099 || *month < 1 || *month > 12) {
        return false;
    }
    static const int month_days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    const int maximum = *month == 2 && leap_year(*year)
        ? 29 : month_days[*month - 1];
    return *day >= 1 && *day <= maximum;
}

static int32_t civil_day_number(int year, int month, int day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned day_of_year =
        (153U * (unsigned)(month + (month > 2 ? -3 : 9)) + 2U) / 5U +
        (unsigned)day - 1U;
    const unsigned day_of_era = year_of_era * 365U + year_of_era / 4U -
        year_of_era / 100U + day_of_year;
    return era * 146097 + (int32_t)day_of_era;
}

static bool date_number(const char *date, int32_t *number) {
    int year = 0;
    int month = 0;
    int day = 0;
    if (!parse_date(date, &year, &month, &day)) {
        return false;
    }
    *number = civil_day_number(year, month, day);
    return true;
}

bool ramadan_date_valid(const char *date) {
    int year = 0;
    int month = 0;
    int day = 0;
    return parse_date(date, &year, &month, &day);
}

bool ramadan_period_valid(const char *start_date, const char *end_date) {
    int32_t start = 0;
    int32_t end = 0;
    if (!date_number(start_date, &start) || !date_number(end_date, &end)) {
        return false;
    }
    const int total_days = end - start + 1;
    return total_days == 29 || total_days == 30;
}

void ramadan_status_for_date(
        const char *start_date,
        const char *end_date,
        const struct tm *local_date,
        ramadan_status_t *status) {
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    if (local_date == NULL || !ramadan_period_valid(start_date, end_date)) {
        return;
    }

    int32_t start = 0;
    int32_t end = 0;
    date_number(start_date, &start);
    date_number(end_date, &end);
    const int32_t today = civil_day_number(
        local_date->tm_year + 1900,
        local_date->tm_mon + 1,
        local_date->tm_mday);

    status->configured = true;
    status->total_days = end - start + 1;
    status->days_until_start = start > today ? start - today : 0;
    if (today >= start && today <= end) {
        status->active = true;
        status->day_number = today - start + 1;
    }
}
