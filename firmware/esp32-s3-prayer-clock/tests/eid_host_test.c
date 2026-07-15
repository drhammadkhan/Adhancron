#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "eid.h"

static struct tm date(int year, int month, int day) {
    return (struct tm){
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
    };
}

int main(void) {
    eid_status_t status;
    struct tm current = date(2027, 3, 10);
    eid_status_for_date("2027-03-10", "2027-05-17", &current, &status);
    assert(status.active && status.kind == EID_AL_FITR);
    assert(status.fitr_configured && status.adha_configured);
    assert(eid_takbeer_due(&status, 420, 420, 540, 15));
    assert(eid_takbeer_due(&status, 540, 420, 540, 15));
    assert(!eid_takbeer_due(&status, 419, 420, 540, 15));
    assert(!eid_takbeer_due(&status, 421, 420, 540, 15));
    assert(eid_latest_takbeer_minute(&status, 420, 420, 540, 15, 2) == 420);
    assert(eid_latest_takbeer_minute(&status, 422, 420, 540, 15, 2) == 420);
    assert(eid_latest_takbeer_minute(&status, 423, 420, 540, 15, 2) == -1);
    assert(eid_latest_takbeer_minute(&status, 541, 420, 540, 15, 2) == 540);
    assert(eid_latest_takbeer_minute(&status, 543, 420, 540, 15, 2) == -1);
    assert(eid_next_takbeer_minute(&status, 419, 420, 540, 15) == 420);
    assert(eid_next_takbeer_minute(&status, 421, 420, 540, 15) == 435);
    assert(eid_next_takbeer_minute(&status, 541, 420, 540, 15) == -1);

    current = date(2027, 5, 17);
    eid_status_for_date("2027-03-10", "2027-05-17", &current, &status);
    assert(status.active && status.kind == EID_AL_ADHA);
    assert(strcmp(eid_kind_name(status.kind), "Eid al-Adha") == 0);

    current = date(2027, 5, 18);
    eid_status_for_date("2027-03-10", "not-a-date", &current, &status);
    assert(!status.active && status.fitr_configured && !status.adha_configured);
    assert(!eid_takbeer_schedule_valid(540, 420, 15));
    assert(!eid_takbeer_schedule_valid(420, 540, 1));
    assert(eid_takbeer_schedule_valid(420, 540, 30));

    puts("Eid date and takbeer schedule fixtures passed.");
    return 0;
}
