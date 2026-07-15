#include "battery_status.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    int millivolts;
    int percentage;
} battery_curve_point_t;

static const battery_curve_point_t BATTERY_CURVE[] = {
    {3300, 0},
    {3500, 5},
    {3600, 10},
    {3700, 25},
    {3800, 45},
    {3900, 65},
    {4000, 80},
    {4100, 90},
    {4200, 100},
};

int battery_percentage_from_millivolts(int millivolts) {
    if (millivolts <= BATTERY_CURVE[0].millivolts) return 0;

    const size_t point_count = sizeof(BATTERY_CURVE) / sizeof(BATTERY_CURVE[0]);
    if (millivolts >= BATTERY_CURVE[point_count - 1].millivolts) return 100;

    for (size_t index = 1; index < point_count; index++) {
        const battery_curve_point_t lower = BATTERY_CURVE[index - 1];
        const battery_curve_point_t upper = BATTERY_CURVE[index];
        if (millivolts <= upper.millivolts) {
            return lower.percentage +
                (millivolts - lower.millivolts) *
                (upper.percentage - lower.percentage) /
                (upper.millivolts - lower.millivolts);
        }
    }
    return 100;
}

void battery_estimator_update(
    battery_estimator_t *estimator,
    int measured_millivolts,
    battery_status_t *status) {
    if (estimator == NULL || status == NULL) return;
    memset(status, 0, sizeof(*status));

    if (measured_millivolts < 2500 || measured_millivolts > 4500) {
        estimator->filtered_millivolts = 0;
        estimator->filtered_millivolts_q8 = 0;
        estimator->trend_reference_millivolts = 0;
        estimator->trend_samples = 0;
        estimator->rising_windows = 0;
        estimator->stable_windows = 0;
        estimator->charging = false;
        return;
    }

    if (estimator->filtered_millivolts == 0) {
        estimator->filtered_millivolts = measured_millivolts;
        estimator->filtered_millivolts_q8 = measured_millivolts * 256;
        estimator->trend_reference_millivolts = measured_millivolts;
    } else {
        estimator->filtered_millivolts_q8 =
            (estimator->filtered_millivolts_q8 * 7 +
                measured_millivolts * 256) / 8;
        estimator->filtered_millivolts =
            (estimator->filtered_millivolts_q8 + 128) / 256;
    }

    estimator->trend_samples++;
    if (estimator->trend_samples >= 30) {
        const int change = estimator->filtered_millivolts -
            estimator->trend_reference_millivolts;
        // The TP4054's rise can be only a few millivolts per minute near the
        // top of the charge curve. Two filtered rising windows reject noise.
        if (change >= 2) {
            estimator->rising_windows++;
            estimator->stable_windows = 0;
            if (estimator->rising_windows >= 2) estimator->charging = true;
        } else if (change <= -3) {
            estimator->rising_windows = 0;
            estimator->stable_windows = 0;
            estimator->charging = false;
        } else {
            estimator->stable_windows++;
            if (estimator->charging && estimator->stable_windows >= 10) {
                estimator->charging = false;
                estimator->rising_windows = 0;
            } else if (!estimator->charging && estimator->stable_windows >= 2) {
                estimator->rising_windows = 0;
            }
        }
        estimator->trend_reference_millivolts = estimator->filtered_millivolts;
        estimator->trend_samples = 0;
    }

    status->available = true;
    status->millivolts = estimator->filtered_millivolts;
    status->percentage = battery_percentage_from_millivolts(status->millivolts);
    status->full = status->millivolts >= 4180;
    status->charging = estimator->charging && !status->full;
}
