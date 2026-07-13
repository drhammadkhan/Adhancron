#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ramadan.h"

static struct tm date(int year, int month, int day) {
    return (struct tm){
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
    };
}

int main(void) {
    assert(ramadan_date_valid("2027-02-08"));
    assert(ramadan_date_valid("2028-02-29"));
    assert(!ramadan_date_valid("2027-02-29"));
    assert(!ramadan_date_valid("27-02-08"));
    assert(ramadan_period_valid("2027-02-08", "2027-03-09"));
    assert(ramadan_period_valid("2027-12-15", "2028-01-12"));
    assert(!ramadan_period_valid("2027-02-08", "2027-03-07"));
    assert(!ramadan_period_valid("2027-03-09", "2027-02-08"));

    ramadan_status_t status;
    struct tm current = date(2027, 2, 7);
    ramadan_status_for_date("2027-02-08", "2027-03-09", &current, &status);
    assert(status.configured && !status.active && status.days_until_start == 1);
    assert(status.total_days == 30);

    current = date(2027, 2, 8);
    ramadan_status_for_date("2027-02-08", "2027-03-09", &current, &status);
    assert(status.active && status.day_number == 1 && status.total_days == 30);

    current = date(2027, 3, 9);
    ramadan_status_for_date("2027-02-08", "2027-03-09", &current, &status);
    assert(status.active && status.day_number == 30);

    current = date(2027, 3, 10);
    ramadan_status_for_date("2027-02-08", "2027-03-09", &current, &status);
    assert(status.configured && !status.active && status.day_number == 0);

    puts("Ramadan date and range fixtures passed.");
    return 0;
}
