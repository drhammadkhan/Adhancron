#include "eid.h"

#include <stdio.h>
#include <string.h>

#include "ramadan.h"

static bool date_matches(const char *date, const struct tm *local_date) {
    if (!ramadan_date_valid(date) || local_date == NULL) return false;
    char today[11];
    if (strftime(today, sizeof(today), "%Y-%m-%d", local_date) != 10) {
        return false;
    }
    return strcmp(date, today) == 0;
}

void eid_status_for_date(
        const char *fitr_date,
        const char *adha_date,
        const struct tm *local_date,
        eid_status_t *status) {
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    status->fitr_configured = ramadan_date_valid(fitr_date);
    status->adha_configured = ramadan_date_valid(adha_date);
    if (date_matches(fitr_date, local_date)) {
        status->active = true;
        status->kind = EID_AL_FITR;
    } else if (date_matches(adha_date, local_date)) {
        status->active = true;
        status->kind = EID_AL_ADHA;
    }
}

const char *eid_kind_name(eid_kind_t kind) {
    if (kind == EID_AL_FITR) return "Eid al-Fitr";
    if (kind == EID_AL_ADHA) return "Eid al-Adha";
    return "Eid";
}

bool eid_takbeer_schedule_valid(
        int start_minute, int end_minute, int interval_minutes) {
    return start_minute >= 0 && start_minute < 1440 &&
        end_minute >= start_minute && end_minute < 1440 &&
        interval_minutes >= 5 && interval_minutes <= 120;
}

bool eid_takbeer_due(
        const eid_status_t *status,
        int current_minute,
        int start_minute,
        int end_minute,
        int interval_minutes) {
    return status != NULL && status->active &&
        eid_takbeer_schedule_valid(start_minute, end_minute, interval_minutes) &&
        current_minute >= start_minute && current_minute <= end_minute &&
        (current_minute - start_minute) % interval_minutes == 0;
}

int eid_latest_takbeer_minute(
        const eid_status_t *status,
        int current_minute,
        int start_minute,
        int end_minute,
        int interval_minutes,
        int recovery_minutes) {
    if (status == NULL || !status->active || recovery_minutes < 0 ||
            !eid_takbeer_schedule_valid(
                start_minute, end_minute, interval_minutes) ||
            current_minute < start_minute) {
        return -1;
    }
    const int capped_minute = current_minute < end_minute
        ? current_minute : end_minute;
    const int elapsed = capped_minute - start_minute;
    const int slot = start_minute +
        (elapsed / interval_minutes) * interval_minutes;
    return current_minute - slot <= recovery_minutes ? slot : -1;
}

int eid_next_takbeer_minute(
        const eid_status_t *status,
        int current_minute,
        int start_minute,
        int end_minute,
        int interval_minutes) {
    if (status == NULL || !status->active ||
            !eid_takbeer_schedule_valid(start_minute, end_minute, interval_minutes) ||
            current_minute > end_minute) {
        return -1;
    }
    if (current_minute <= start_minute) return start_minute;
    const int elapsed = current_minute - start_minute;
    const int remainder = elapsed % interval_minutes;
    const int next = remainder == 0
        ? current_minute
        : current_minute + interval_minutes - remainder;
    return next <= end_minute ? next : -1;
}
