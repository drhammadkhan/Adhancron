#include "prayer_times.h"

#include <math.h>
#include <stdbool.h>

static const double PI = 3.14159265358979323846;

static double radians(double degrees) { return degrees * PI / 180.0; }
static double degrees(double radians_value) { return radians_value * 180.0 / PI; }
static double clamp(double value, double low, double high) {
    return value < low ? low : (value > high ? high : value);
}

static int normalise_minutes(int minutes) {
    minutes %= 24 * 60;
    return minutes < 0 ? minutes + 24 * 60 : minutes;
}

static int days_since_solstice(const struct tm *date, double latitude) {
    const int days_in_year = ((date->tm_year + 1900) % 4 == 0 && ((date->tm_year + 1900) % 100 != 0 || (date->tm_year + 1900) % 400 == 0)) ? 366 : 365;
    int days = latitude >= 0 ? date->tm_yday + 11 : date->tm_yday - (days_in_year == 366 ? 173 : 172);
    days %= days_in_year;
    return days < 0 ? days + days_in_year : days;
}

static double seasonal_isha_offset_minutes(const struct tm *date, double latitude) {
    const double abs_lat = fabs(latitude);
    const double a = 75.0 + (25.6 / 55.0) * abs_lat;
    const double b = 75.0 + (2.05 / 55.0) * abs_lat;
    const double c = 75.0 - (9.21 / 55.0) * abs_lat;
    const double d = 75.0 + (6.14 / 55.0) * abs_lat;
    const int days = days_since_solstice(date, latitude);
    if (days < 91) return a + (b - a) * days / 91.0;
    if (days < 137) return b + (c - b) * (days - 91) / 46.0;
    if (days < 183) return c + (d - c) * (days - 137) / 46.0;
    if (days < 229) return d + (c - d) * (days - 183) / 46.0;
    if (days < 275) return c + (b - c) * (days - 229) / 46.0;
    return b + (a - b) * (days - 275) / 91.0;
}

static double julian_day(const struct tm *date) {
    int year=date->tm_year+1900,month=date->tm_mon+1,day=date->tm_mday;
    if(month<=2){year--;month+=12;} int a=year/100,b=2-a+a/4;
    return floor(365.25*(year+4716))+floor(30.6001*(month+1))+day+b-1524.5;
}

static void solar_values(double century, double *declination, double *equation_of_time) {
    const double l0=fmod(280.46646+century*(36000.76983+0.0003032*century),360.0);
    const double m=357.52911+century*(35999.05029-0.0001537*century);
    const double e=0.016708634-century*(0.000042037+0.0000001267*century);
    const double center=sin(radians(m))*(1.914602-century*(0.004817+0.000014*century))+sin(radians(2*m))*(0.019993-0.000101*century)+sin(radians(3*m))*0.000289;
    const double omega=125.04-1934.136*century;
    const double lambda=l0+center-0.00569-0.00478*sin(radians(omega));
    const double seconds=21.448-century*(46.815+century*(0.00059-century*0.001813));
    const double obliquity=23.0+(26.0+seconds/60.0)/60.0+0.00256*cos(radians(omega));
    *declination=asin(sin(radians(obliquity))*sin(radians(lambda)));
    const double y=pow(tan(radians(obliquity)/2.0),2);
    const double etime=y*sin(2*radians(l0))-2*e*sin(radians(m))+4*e*y*sin(radians(m))*cos(2*radians(l0))-0.5*y*y*sin(4*radians(l0))-1.25*e*e*sin(2*radians(m));
    *equation_of_time=degrees(etime)*4.0;
}

static double hour_angle(double latitude, double declination, double elevation_degrees) {
    const double numerator = sin(radians(elevation_degrees)) - sin(radians(latitude)) * sin(declination);
    const double denominator = cos(radians(latitude)) * cos(declination);
    if (fabs(denominator) < 0.000001) {
        return NAN;
    }
    const double value = numerator / denominator;
    if (value < -1.0 || value > 1.0) {
        return NAN;
    }
    return degrees(acos(clamp(value, -1.0, 1.0))) / 15.0;
}

static double event_utc_minutes(double jd, double latitude, double longitude, double elevation, bool setting) {
    double adjustment=0,time_utc=0;
    for(int iteration=0;iteration<2;iteration++){
        double declination,equation; solar_values((jd+adjustment-2451545.0)/36525.0,&declination,&equation);
        const double angle=hour_angle(latitude,declination,elevation);
        if(isnan(angle)) return NAN;
        time_utc=720.0+(-longitude+(setting?angle*15.0:-angle*15.0))*4.0-equation;
        adjustment=time_utc/1440.0;
    }
    return time_utc;
}

bool prayer_times_calculate(const struct tm *local_date,
                            const prayer_calculation_config_t *config,
                            prayer_times_t *times) {
    if (local_date == NULL || config == NULL || times == NULL ||
        config->latitude < -89.0 || config->latitude > 89.0 ||
        config->longitude < -180.0 || config->longitude > 180.0) {
        return false;
    }

    const double jd=julian_day(local_date); double declination=0.0,equation=0.0;
    solar_values((jd-2451545.0)/36525.0,&declination,&equation);

    // mktime/localtime account for the active timezone and DST rule.
    struct tm noon = *local_date;
    noon.tm_hour = 12;
    noon.tm_min = 0;
    noon.tm_sec = 0;
    mktime(&noon);
    char offset_text[8] = {0};
    if (strftime(offset_text, sizeof(offset_text), "%z", &noon) == 0) return false;
    const int sign = offset_text[0] == '-' ? -1 : 1;
    const int utc_offset_minutes = sign * (((offset_text[1] - '0') * 10 + offset_text[2] - '0') * 60
        + (offset_text[3] - '0') * 10 + offset_text[4] - '0');
    const double solar_noon = 720.0 - 4.0 * config->longitude - equation + utc_offset_minutes;
    const double sunrise_utc=event_utc_minutes(jd,config->latitude,config->longitude,-0.833,false);
    const double sunset_utc=event_utc_minutes(jd,config->latitude,config->longitude,-0.833,true);
    const double asr_elevation = degrees(atan(1.0 / (1.0 + tan(fabs(radians(config->latitude) - declination)))));
    const double asr_utc=event_utc_minutes(jd,config->latitude,config->longitude,asr_elevation,true);
    if (isnan(sunrise_utc) || isnan(sunset_utc) || isnan(asr_utc)) {
        return false;
    }

    times->fajr_minutes = normalise_minutes((int)lround(sunrise_utc + utc_offset_minutes - 90.0));
    times->sunrise_minutes = normalise_minutes((int)lround(sunrise_utc + utc_offset_minutes));
    times->dhuhr_minutes = normalise_minutes((int)lround(solar_noon + 5.0));
    times->asr_minutes = normalise_minutes((int)lround(asr_utc + utc_offset_minutes));
    times->maghrib_minutes = normalise_minutes((int)lround(sunset_utc + utc_offset_minutes + 1.0));
    times->isha_minutes = normalise_minutes((int)lround(sunset_utc + utc_offset_minutes + seasonal_isha_offset_minutes(local_date, config->latitude)));
    return true;
}

const char *prayer_time_name(int index) {
    static const char *names[] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
    return index >= 0 && index < 6 ? names[index] : "Unknown";
}

int prayer_time_minutes(const prayer_times_t *times, int index) {
    const int *values = (const int *)times;
    return index >= 0 && index < 6 ? values[index] : -1;
}
