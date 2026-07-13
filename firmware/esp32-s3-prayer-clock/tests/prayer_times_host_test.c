#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "prayer_times.h"

typedef struct { const char *name; const char *tz; int year, month, day; double lat, lon; int expected[6]; } fixture_t;

int main(void) {
    const fixture_t fixtures[] = {
        {"London winter","GMT0BST,M3.5.0/1,M10.5.0/2",2026,1,15,51.5074,-.1278,{390,481,735,843,981,1073}},
        {"London summer","GMT0BST,M3.5.0/1,M10.5.0/2",2026,6,15,51.5074,-.1278,{193,283,786,1044,1280,1358}},
        {"London December","GMT0BST,M3.5.0/1,M10.5.0/2",2026,12,15,51.5074,-.1278,{390,481,720,816,952,1048}},
        {"Oshkosh winter","CST6CDT,M3.2.0/2,M11.1.0/2",2026,1,15,44.0247,-88.5426,{356,446,728,864,1003,1092}},
        {"Oshkosh summer","CST6CDT,M3.2.0/2,M11.1.0/2",2026,6,15,44.0247,-88.5426,{221,311,780,1022,1240,1317}},
        {"Oshkosh December","CST6CDT,M3.2.0/2,M11.1.0/2",2026,12,15,44.0247,-88.5426,{352,442,714,839,977,1070}},
    };
    int failures = 0;
    for (size_t index = 0; index < sizeof(fixtures)/sizeof(fixtures[0]); index++) {
        const fixture_t *fixture = &fixtures[index]; setenv("TZ", fixture->tz, 1); tzset();
        struct tm date = {.tm_year=fixture->year-1900,.tm_mon=fixture->month-1,.tm_mday=fixture->day,.tm_hour=12,.tm_isdst=-1}; mktime(&date);
        prayer_calculation_config_t config = {.latitude=fixture->lat,.longitude=fixture->lon}; prayer_times_t result;
        if (!prayer_times_calculate(&date,&config,&result)) { fprintf(stderr,"%s failed to calculate\n",fixture->name); failures++; continue; }
        for (int prayer=0;prayer<6;prayer++) { int actual=prayer_time_minutes(&result,prayer),delta=abs(actual-fixture->expected[prayer]); if(delta>2){fprintf(stderr,"%s %s expected %d got %d\n",fixture->name,prayer_time_name(prayer),fixture->expected[prayer],actual);failures++;} }
    }
    if (failures) return 1;
    puts("Prayer-time fixtures passed within two minutes."); return 0;
}
