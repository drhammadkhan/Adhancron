#pragma once

#include <stdbool.h>
#include <time.h>

typedef enum {
    EID_NONE = 0,
    EID_AL_FITR,
    EID_AL_ADHA,
} eid_kind_t;

typedef struct {
    bool fitr_configured;
    bool adha_configured;
    bool active;
    eid_kind_t kind;
} eid_status_t;

void eid_status_for_date(
    const char *fitr_date,
    const char *adha_date,
    const struct tm *local_date,
    eid_status_t *status);
const char *eid_kind_name(eid_kind_t kind);
bool eid_takbeer_schedule_valid(int start_minute, int end_minute, int interval_minutes);
bool eid_takbeer_due(
    const eid_status_t *status,
    int current_minute,
    int start_minute,
    int end_minute,
    int interval_minutes);
int eid_latest_takbeer_minute(
    const eid_status_t *status,
    int current_minute,
    int start_minute,
    int end_minute,
    int interval_minutes,
    int recovery_minutes);
int eid_next_takbeer_minute(
    const eid_status_t *status,
    int current_minute,
    int start_minute,
    int end_minute,
    int interval_minutes);
