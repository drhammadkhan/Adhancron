#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "battery_status.h"

static battery_status_t update_repeatedly(
    battery_estimator_t *estimator,
    int millivolts,
    int count) {
    battery_status_t status = {0};
    for (int sample = 0; sample < count; sample++) {
        battery_estimator_update(estimator, millivolts, &status);
    }
    return status;
}

int main(void) {
    assert(battery_percentage_from_millivolts(3200) == 0);
    assert(battery_percentage_from_millivolts(3500) == 5);
    assert(battery_percentage_from_millivolts(3750) == 35);
    assert(battery_percentage_from_millivolts(4050) == 85);
    assert(battery_percentage_from_millivolts(4250) == 100);

    battery_estimator_t estimator = {0};
    battery_status_t status = {0};
    battery_estimator_update(&estimator, 0, &status);
    assert(!status.available);

    status = update_repeatedly(&estimator, 3700, 30);
    assert(status.available);
    assert(status.percentage == 25);
    assert(!status.charging);

    status = update_repeatedly(&estimator, 3750, 30);
    assert(!status.charging);
    status = update_repeatedly(&estimator, 3800, 30);
    assert(status.charging);

    status = update_repeatedly(&estimator, 3700, 30);
    assert(!status.charging);

    status = update_repeatedly(&estimator, 4200, 90);
    assert(status.full);
    assert(!status.charging);

    puts("Battery percentage and trend fixtures passed.");
    return 0;
}
