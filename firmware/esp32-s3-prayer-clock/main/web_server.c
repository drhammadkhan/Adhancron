#include "web_server.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_storage.h"
#include "battery_monitor.h"
#include "cast_sender.h"
#include "dlna_sender.h"
#include "eid.h"
#include "firmware_update.h"
#include "prayer_times.h"
#include "ramadan.h"

static const char *TAG = "adhan_web";
static adhan_settings_t *current_settings;
static bool *storage_mounted;
static bool *adhan_audio_available;
static bool *takbeer_audio_available;
static web_play_callback_t play_audio;

extern const uint8_t web_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t web_wifi_html_start[] asm("_binary_wifi_html_start");
extern const uint8_t web_location_html_start[] asm("_binary_location_html_start");
extern const uint8_t web_app_css_start[] asm("_binary_app_css_start");
extern const uint8_t web_app_js_start[] asm("_binary_app_js_start");

static void restart_task(void *unused) {
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

typedef enum { PAGE_AUTOMATIC, PAGE_LOCATION, PAGE_WIFI } page_kind_t;

static void format_minutes(int minutes, char output[6]);

static esp_err_t render_page(httpd_req_t *request, page_kind_t kind) {
    const uint8_t *page = web_index_html_start;
    if (kind == PAGE_WIFI || (kind == PAGE_AUTOMATIC && !settings_has_wifi(current_settings))) {
        page = web_wifi_html_start;
    } else if (kind == PAGE_LOCATION || (kind == PAGE_AUTOMATIC && !current_settings->location_configured)) {
        page = web_location_html_start;
    }
    httpd_resp_set_type(request, "text/html");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    return httpd_resp_sendstr(request, (const char *)page);
}

static esp_err_t page_handler(httpd_req_t *request) { return render_page(request, PAGE_AUTOMATIC); }
static esp_err_t location_page_handler(httpd_req_t *request) { return render_page(request, PAGE_LOCATION); }
static esp_err_t wifi_page_handler(httpd_req_t *request) { return render_page(request, PAGE_WIFI); }

static esp_err_t stylesheet_handler(httpd_req_t *request) {
    httpd_resp_set_type(request, "text/css");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    return httpd_resp_sendstr(request, (const char *)web_app_css_start);
}

static esp_err_t script_handler(httpd_req_t *request) {
    httpd_resp_set_type(request, "application/javascript");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    return httpd_resp_sendstr(request, (const char *)web_app_js_start);
}

static void timezone_for_location(adhan_settings_t *settings) {
    const double lat = settings->latitude;
    const double lon = settings->longitude;
    if (lat >= 49.0 && lat <= 61.0 && lon >= -9.0 && lon <= 3.0) strlcpy(settings->timezone, "GMT0BST,M3.5.0/1,M10.5.0/2", sizeof(settings->timezone));
    else if (lat >= 24.0 && lat <= 50.0 && lon >= -82.0 && lon <= -66.0) strlcpy(settings->timezone, "EST5EDT,M3.2.0/2,M11.1.0/2", sizeof(settings->timezone));
    else if (lat >= 24.0 && lat <= 50.0 && lon >= -103.0 && lon < -82.0) strlcpy(settings->timezone, "CST6CDT,M3.2.0/2,M11.1.0/2", sizeof(settings->timezone));
    else if (lat >= 24.0 && lat <= 50.0 && lon >= -115.0 && lon < -103.0) strlcpy(settings->timezone, "MST7MDT,M3.2.0/2,M11.1.0/2", sizeof(settings->timezone));
    else if (lat >= 24.0 && lat <= 50.0 && lon >= -125.0 && lon < -115.0) strlcpy(settings->timezone, "PST8PDT,M3.2.0/2,M11.1.0/2", sizeof(settings->timezone));
    else snprintf(settings->timezone, sizeof(settings->timezone), "UTC%+d", -(int)lround(lon / 15.0));
}

static void timezone_from_name(adhan_settings_t *settings, const char *name) {
    if (strcmp(name, "Europe/London") == 0 || strcmp(name, "Europe/Dublin") == 0) strlcpy(settings->timezone, "GMT0BST,M3.5.0/1,M10.5.0/2", sizeof(settings->timezone));
    else if (strstr(name, "America/New_York") || strstr(name, "America/Detroit")) strlcpy(settings->timezone, "EST5EDT,M3.2.0/2,M11.1.0/2", sizeof(settings->timezone));
    else if (strstr(name, "America/Chicago")) strlcpy(settings->timezone, "CST6CDT,M3.2.0/2,M11.1.0/2", sizeof(settings->timezone));
    else if (strstr(name, "America/Denver")) strlcpy(settings->timezone, "MST7MDT,M3.2.0/2,M11.1.0/2", sizeof(settings->timezone));
    else if (strstr(name, "America/Los_Angeles")) strlcpy(settings->timezone, "PST8PDT,M3.2.0/2,M11.1.0/2", sizeof(settings->timezone));
    else if (strncmp(name, "Europe/", 7) == 0) strlcpy(settings->timezone, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(settings->timezone));
    else if (strcmp(name, "Asia/Kolkata") == 0 || strcmp(name, "Asia/Calcutta") == 0) strlcpy(settings->timezone, "IST-5:30", sizeof(settings->timezone));
    else if (strcmp(name, "Asia/Dubai") == 0) strlcpy(settings->timezone, "GST-4", sizeof(settings->timezone));
    else if (strcmp(name, "Asia/Karachi") == 0) strlcpy(settings->timezone, "PKT-5", sizeof(settings->timezone));
    else if (strcmp(name, "America/Phoenix") == 0) strlcpy(settings->timezone, "MST7", sizeof(settings->timezone));
    else if (strstr(name, "America/Toronto")) strlcpy(settings->timezone, "EST5EDT,M3.2.0/2,M11.1.0/2", sizeof(settings->timezone));
    else if (strstr(name, "America/Vancouver")) strlcpy(settings->timezone, "PST8PDT,M3.2.0/2,M11.1.0/2", sizeof(settings->timezone));
    else if (strncmp(name, "Australia/", 10) == 0) strlcpy(settings->timezone, "AEST-10AEDT,M10.1.0,M4.1.0/3", sizeof(settings->timezone));
    else if (strcmp(name, "Pacific/Auckland") == 0) strlcpy(settings->timezone, "NZST-12NZDT,M9.5.0,M4.1.0/3", sizeof(settings->timezone));
    else if (strcmp(name, "UTC") == 0 || strcmp(name, "Etc/UTC") == 0) strlcpy(settings->timezone, "UTC0", sizeof(settings->timezone));
    else timezone_for_location(settings);
}

static void get_value(const char *body, const char *key, char *value, size_t value_size) {
    if (httpd_query_key_value(body, key, value, value_size) != ESP_OK) {
        value[0] = '\0';
        return;
    }
    char *source = value;
    char *destination = value;
    while (*source != '\0') {
        if (*source == '+') {
            *destination++ = ' ';
            source++;
        } else if (*source == '%' && source[1] != '\0' && source[2] != '\0') {
            char encoded[3] = {source[1], source[2], '\0'};
            char *end = NULL;
            const long decoded = strtol(encoded, &end, 16);
            if (end == encoded + 2) {
                *destination++ = (char)decoded;
                source += 3;
            } else {
                *destination++ = *source++;
            }
        } else {
            *destination++ = *source++;
        }
    }
    *destination = '\0';
}

static bool parse_time_value(const char *value, int *minutes) {
    int hour = 0;
    int minute = 0;
    char trailing = '\0';
    if (value == NULL || minutes == NULL ||
            sscanf(value, "%2d:%2d%c", &hour, &minute, &trailing) != 2 ||
            hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    *minutes = hour * 60 + minute;
    return true;
}

static esp_err_t settings_handler(httpd_req_t *request) {
    if (request->content_len > 3072) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Settings are too large");
    }
    char body[3073] = {0};
    int received = 0;
    while (received < request->content_len) {
        const int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }
    char value[128];
    get_value(body, "step", value, sizeof(value));
    const bool wifi_step = strcmp(value, "wifi") == 0;
    if (wifi_step) {
        get_value(body, "ssid", current_settings->wifi_ssid, sizeof(current_settings->wifi_ssid));
        get_value(body, "password", current_settings->wifi_password, sizeof(current_settings->wifi_password));
        if (!settings_has_wifi(current_settings)) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Choose or enter a Wi-Fi network");
    } else if (strcmp(value, "location") == 0) {
        get_value(body, "found", value, sizeof(value));
        if (strcmp(value, "1") != 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Find and confirm a location first");
        char latitude_value[32] = {0}; char longitude_value[32] = {0};
        get_value(body, "latitude", latitude_value, sizeof(latitude_value)); char *latitude_end = NULL; const double latitude = strtod(latitude_value, &latitude_end);
        get_value(body, "longitude", longitude_value, sizeof(longitude_value)); char *longitude_end = NULL; const double longitude = strtod(longitude_value, &longitude_end);
        if (latitude_end == latitude_value || *latitude_end != '\0' || longitude_end == longitude_value || *longitude_end != '\0' || !isfinite(latitude) || !isfinite(longitude) || latitude < -89.0 || latitude > 89.0 || longitude < -180.0 || longitude > 180.0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "The selected location is invalid");
        current_settings->latitude = latitude; current_settings->longitude = longitude;
        get_value(body, "timezone", value, sizeof(value)); timezone_from_name(current_settings, value);
        get_value(body, "location_name", current_settings->location_name, sizeof(current_settings->location_name));
        if (current_settings->location_name[0] == '\0') {
            snprintf(current_settings->location_name, sizeof(current_settings->location_name),
                "%.4f, %.4f", latitude, longitude);
        }
        current_settings->location_configured = true;
        get_value(body, "volume", value, sizeof(value)); current_settings->volume = atoi(value);
    } else if (strcmp(value, "playback") == 0) {
        char ramadan_start[16] = {0};
        char ramadan_end[16] = {0};
        char eid_fitr[16] = {0};
        char eid_adha[16] = {0};
        get_value(body, "ramadan_start", ramadan_start, sizeof(ramadan_start));
        get_value(body, "ramadan_end", ramadan_end, sizeof(ramadan_end));
        const bool ramadan_empty = ramadan_start[0] == '\0' && ramadan_end[0] == '\0';
        if (!ramadan_empty && !ramadan_period_valid(ramadan_start, ramadan_end)) {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                "Choose both Ramadan dates and make the inclusive period 29 or 30 days");
        }
        get_value(body, "eid_fitr", eid_fitr, sizeof(eid_fitr));
        get_value(body, "eid_adha", eid_adha, sizeof(eid_adha));
        if ((eid_fitr[0] != '\0' && !ramadan_date_valid(eid_fitr)) ||
                (eid_adha[0] != '\0' && !ramadan_date_valid(eid_adha))) {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                "Choose valid dates for Eid al-Fitr and Eid al-Adha");
        }
        char eid_start_value[8] = {0};
        char eid_end_value[8] = {0};
        char eid_interval_value[8] = {0};
        int eid_start = 0;
        int eid_end = 0;
        get_value(body, "eid_takbeer_start", eid_start_value,
            sizeof(eid_start_value));
        get_value(body, "eid_takbeer_end", eid_end_value,
            sizeof(eid_end_value));
        get_value(body, "eid_takbeer_interval", eid_interval_value,
            sizeof(eid_interval_value));
        char *interval_end = NULL;
        const long eid_interval = strtol(
            eid_interval_value, &interval_end, 10);
        if (!parse_time_value(eid_start_value, &eid_start) ||
                !parse_time_value(eid_end_value, &eid_end) ||
                interval_end == eid_interval_value || *interval_end != '\0' ||
                !eid_takbeer_schedule_valid(
                    eid_start, eid_end, (int)eid_interval)) {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                "Choose an Eid takbeer window within one day and an interval from 5 to 120 minutes");
        }
        const char *keys[] = {"fajr","dhuhr","asr","maghrib","isha"};
        for (int index=0;index<5;index++) { get_value(body,keys[index],value,sizeof(value)); current_settings->enabled[index]=value[0]!='\0'; }
        get_value(body, "volume", value, sizeof(value)); current_settings->volume = atoi(value);
        get_value(body, "automatic_updates", value, sizeof(value));
        current_settings->automatic_updates = value[0] != '\0';
        get_value(body, "display_style", value, sizeof(value));
        current_settings->display_style = strcmp(value, "focus") == 0
            ? ADHAN_DISPLAY_FOCUS : ADHAN_DISPLAY_DETAILED;
        strlcpy(current_settings->ramadan_start_date, ramadan_start,
            sizeof(current_settings->ramadan_start_date));
        strlcpy(current_settings->ramadan_end_date, ramadan_end,
            sizeof(current_settings->ramadan_end_date));
        strlcpy(current_settings->eid_fitr_date, eid_fitr,
            sizeof(current_settings->eid_fitr_date));
        strlcpy(current_settings->eid_adha_date, eid_adha,
            sizeof(current_settings->eid_adha_date));
        current_settings->eid_takbeer_start_minute = eid_start;
        current_settings->eid_takbeer_end_minute = eid_end;
        current_settings->eid_takbeer_interval_minutes = (int)eid_interval;
        get_value(body, "output", value, sizeof(value));
        current_settings->output = strcmp(value, "cast") == 0
            ? ADHAN_OUTPUT_CAST
            : strcmp(value, "dlna") == 0
                ? ADHAN_OUTPUT_DLNA : ADHAN_OUTPUT_ATTACHED;
        if (current_settings->output == ADHAN_OUTPUT_CAST) {
            get_value(body, "cast_device_id", current_settings->cast_device_id,
                sizeof(current_settings->cast_device_id));
            get_value(body, "cast_device_name", current_settings->cast_device_name,
                sizeof(current_settings->cast_device_name));
            if (current_settings->cast_device_id[0] == '\0') {
                return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                    "Choose a Google Cast speaker");
            }
            if (current_settings->cast_device_name[0] == '\0') {
                strlcpy(current_settings->cast_device_name,
                    current_settings->cast_device_id,
                    sizeof(current_settings->cast_device_name));
            }
        } else if (current_settings->output == ADHAN_OUTPUT_DLNA) {
            get_value(body, "dlna_device_url",
                current_settings->dlna_device_url,
                sizeof(current_settings->dlna_device_url));
            get_value(body, "dlna_device_name",
                current_settings->dlna_device_name,
                sizeof(current_settings->dlna_device_name));
            if (strncmp(current_settings->dlna_device_url, "http://", 7) != 0 &&
                    strncmp(current_settings->dlna_device_url, "https://", 8) != 0) {
                return httpd_resp_send_err(
                    request, HTTPD_400_BAD_REQUEST,
                    "Choose a Sonos or DLNA speaker");
            }
            if (current_settings->dlna_device_name[0] == '\0') {
                strlcpy(current_settings->dlna_device_name, "DLNA speaker",
                    sizeof(current_settings->dlna_device_name));
            }
        }
    } else {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "Unknown setup step");
    }
    current_settings->volume = current_settings->volume < 0 ? 0 : (current_settings->volume > 100 ? 100 : current_settings->volume);
    if (!settings_save(current_settings)) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save settings");
    httpd_resp_sendstr(request, wifi_step
        ? "Wi-Fi saved. The prayer clock is restarting and will connect to your network."
        : "Settings saved. The prayer clock is restarting.");
    xTaskCreate(restart_task, "restart", 2048, NULL, 2, NULL);
    return ESP_OK;
}

static bool append_json_string(char *json, size_t capacity, size_t *length, const uint8_t *value);

static void format_minutes(int minutes, char output[6]) { unsigned value=(unsigned)(((minutes%1440)+1440)%1440); snprintf(output, 6, "%02u:%02u", value/60, value%60); }

static esp_err_t status_handler(httpd_req_t *request) {
    char json[4800];
    size_t length = strlcpy(json, "{\"location\":", sizeof(json));
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)current_settings->location_name)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    const char *playback_output =
        current_settings->output == ADHAN_OUTPUT_CAST ? "cast" :
        current_settings->output == ADHAN_OUTPUT_DLNA ? "dlna" : "attached";
    char eid_start[6];
    char eid_end[6];
    format_minutes(current_settings->eid_takbeer_start_minute, eid_start);
    format_minutes(current_settings->eid_takbeer_end_minute, eid_end);
    int written = snprintf(json + length, sizeof(json) - length,
        ",\"playback_output\":\"%s\",\"display_style\":\"%s\",\"volume\":%d,"
        "\"automatic_updates\":%s,"
        "\"enabled_fajr\":%s,\"enabled_dhuhr\":%s,"
        "\"enabled_asr\":%s,\"enabled_maghrib\":%s,\"enabled_isha\":%s,"
        "\"ramadan_start\":\"%s\",\"ramadan_end\":\"%s\","
        "\"eid_fitr\":\"%s\",\"eid_adha\":\"%s\","
        "\"eid_takbeer_start\":\"%s\",\"eid_takbeer_end\":\"%s\","
        "\"eid_takbeer_interval\":%d,\"cast_device_id\":",
        playback_output,
        current_settings->display_style == ADHAN_DISPLAY_FOCUS
            ? "focus" : "detailed",
        current_settings->volume,
        current_settings->automatic_updates ? "true" : "false",
        current_settings->enabled[0] ? "true" : "false",
        current_settings->enabled[1] ? "true" : "false",
        current_settings->enabled[2] ? "true" : "false",
        current_settings->enabled[3] ? "true" : "false",
        current_settings->enabled[4] ? "true" : "false",
        current_settings->ramadan_start_date,
        current_settings->ramadan_end_date,
        current_settings->eid_fitr_date,
        current_settings->eid_adha_date,
        eid_start, eid_end,
        current_settings->eid_takbeer_interval_minutes);
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)current_settings->cast_device_id)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    written = snprintf(json + length, sizeof(json) - length, ",\"cast_device_name\":");
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)current_settings->cast_device_name)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    written = snprintf(
        json + length, sizeof(json) - length, ",\"dlna_device_url\":");
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)current_settings->dlna_device_url)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    written = snprintf(
        json + length, sizeof(json) - length, ",\"dlna_device_name\":");
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)current_settings->dlna_device_name)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    firmware_update_status_t firmware = {0};
    firmware_update_get_status(&firmware);
    written = snprintf(json + length, sizeof(json) - length,
        ",\"firmware_version\":");
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)firmware.current_version)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    written = snprintf(json + length, sizeof(json) - length,
        ",\"update_status\":");
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)firmware.message)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    written = snprintf(json + length, sizeof(json) - length,
        ",\"update_running\":%s", firmware.running ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    battery_status_t battery = {0};
    battery_monitor_get_status(&battery);
    written = snprintf(json + length, sizeof(json) - length,
        ",\"battery_available\":%s,\"battery_percentage\":%d,"
        "\"battery_millivolts\":%d,\"battery_charging\":%s,\"battery_full\":%s",
        battery.available ? "true" : "false", battery.percentage,
        battery.millivolts, battery.charging ? "true" : "false",
        battery.full ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    written = snprintf(json + length, sizeof(json) - length,
        ",\"adhan_audio_available\":%s,\"takbeer_audio_available\":%s",
        adhan_audio_available && *adhan_audio_available ? "true" : "false",
        takbeer_audio_available && *takbeer_audio_available ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    const time_t now = time(NULL); struct tm local_now = {0}; localtime_r(&now, &local_now);
    char device_date[32] = {0};
    strftime(device_date, sizeof(device_date), "%A %e %B", &local_now);
    written = snprintf(json + length, sizeof(json) - length,
        ",\"device_date\":\"%s\",\"device_time\":\"%02d:%02d\","
        "\"device_minute\":%d",
        device_date, local_now.tm_hour, local_now.tm_min,
        local_now.tm_hour * 60 + local_now.tm_min);
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    ramadan_status_t ramadan = {0};
    ramadan_status_for_date(
        current_settings->ramadan_start_date,
        current_settings->ramadan_end_date,
        &local_now,
        &ramadan);
    char ramadan_message[128];
    if (ramadan.active) {
        snprintf(ramadan_message, sizeof(ramadan_message),
            "Ramadan Day %d of %d - Sehri ends at Fajr and Iftar begins at Maghrib",
            ramadan.day_number, ramadan.total_days);
    } else if (ramadan.configured && ramadan.days_until_start > 0) {
        snprintf(ramadan_message, sizeof(ramadan_message),
            "Ramadan begins in %d day%s",
            ramadan.days_until_start, ramadan.days_until_start == 1 ? "" : "s");
    } else if (ramadan.configured) {
        strlcpy(ramadan_message, "The saved Ramadan period has finished",
            sizeof(ramadan_message));
    } else {
        strlcpy(ramadan_message,
            "Set the first and final fasting day to enable Ramadan mode",
            sizeof(ramadan_message));
    }
    written = snprintf(json + length, sizeof(json) - length,
        ",\"ramadan_active\":%s,\"ramadan_day\":%d,\"ramadan_total_days\":%d,\"ramadan_message\":",
        ramadan.active ? "true" : "false", ramadan.day_number, ramadan.total_days);
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)ramadan_message)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    eid_status_t eid = {0};
    eid_status_for_date(
        current_settings->eid_fitr_date,
        current_settings->eid_adha_date,
        &local_now,
        &eid);
    char eid_message[192];
    if (eid.active) {
        snprintf(eid_message, sizeof(eid_message),
            "%s today - takbeer every %d minutes from %s to %s%s",
            eid_kind_name(eid.kind),
            current_settings->eid_takbeer_interval_minutes,
            eid_start, eid_end,
            takbeer_audio_available && *takbeer_audio_available
                ? "" : " - upload the takbeer MP3 before playback");
    } else if (eid.fitr_configured || eid.adha_configured) {
        snprintf(eid_message, sizeof(eid_message),
            "Eid al-Fitr: %s - Eid al-Adha: %s - takbeer %s to %s every %d minutes",
            eid.fitr_configured ? current_settings->eid_fitr_date : "not set",
            eid.adha_configured ? current_settings->eid_adha_date : "not set",
            eid_start, eid_end,
            current_settings->eid_takbeer_interval_minutes);
    } else {
        strlcpy(eid_message,
            "Set your locally observed Eid dates to enable the greeting and takbeer schedule",
            sizeof(eid_message));
    }
    written = snprintf(json + length, sizeof(json) - length,
        ",\"eid_active\":%s,\"eid_kind\":",
        eid.active ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)(eid.active ? eid_kind_name(eid.kind) : ""))) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    written = snprintf(json + length, sizeof(json) - length,
        ",\"eid_message\":");
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    length += written;
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)eid_message)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    prayer_times_t times; prayer_calculation_config_t config = {.latitude = current_settings->latitude, .longitude = current_settings->longitude};
    written = 0;
    if (!current_settings->location_configured || local_now.tm_year < 120 || !prayer_times_calculate(&local_now, &config, &times)) {
        written = snprintf(json + length, sizeof(json) - length,
            ",\"message\":\"Waiting for location and clock sync\",\"prayers\":[]}");
    } else {
        char values[6][6]; for (int i = 0; i < 6; i++) format_minutes(prayer_time_minutes(&times, i), values[i]);
        const char *storage = storage_mounted && *storage_mounted
            ? (adhan_audio_available && *adhan_audio_available
                ? (takbeer_audio_available && *takbeer_audio_available
                    ? "Adhan and Eid takbeer audio ready"
                    : "Adhan ready; upload an Eid takbeer MP3")
                : "Upload an adhan MP3")
            : "Internal audio storage unavailable";
        written = snprintf(json + length, sizeof(json) - length, ",\"message\":\"%04d-%02d-%02d %02d:%02d - %s\",\"prayers\":[{\"name\":\"Fajr\",\"time\":\"%s\"},{\"name\":\"Sunrise\",\"time\":\"%s\"},{\"name\":\"Dhuhr\",\"time\":\"%s\"},{\"name\":\"Asr\",\"time\":\"%s\"},{\"name\":\"Maghrib\",\"time\":\"%s\"},{\"name\":\"Isha\",\"time\":\"%s\"}]}", local_now.tm_year+1900,local_now.tm_mon+1,local_now.tm_mday,local_now.tm_hour,local_now.tm_min,storage,values[0],values[1],values[2],values[3],values[4],values[5]);
    }
    if (written < 0 || (size_t)written >= sizeof(json) - length) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    httpd_resp_set_type(request, "application/json"); return httpd_resp_sendstr(request, json);
}

static const char *signal_label(int rssi) {
    if (rssi >= -55) return "excellent";
    if (rssi >= -67) return "good";
    if (rssi >= -75) return "fair";
    return "weak";
}

static bool append_json_string(char *json, size_t capacity, size_t *length, const uint8_t *value) {
    if (*length + 2 >= capacity) return false;
    json[(*length)++] = '"';
    for (const uint8_t *cursor = value; *cursor != '\0'; cursor++) {
        if (*cursor < 0x20) continue;
        if (*cursor == '"' || *cursor == '\\') {
            if (*length + 2 >= capacity) return false;
            json[(*length)++] = '\\';
        } else if (*length + 1 >= capacity) {
            return false;
        }
        json[(*length)++] = (char)*cursor;
    }
    if (*length + 1 >= capacity) return false;
    json[(*length)++] = '"';
    json[*length] = '\0';
    return true;
}

static esp_err_t networks_handler(httpd_req_t *request) {
    wifi_scan_config_t scan = {.show_hidden = false};
    const esp_err_t scan_result = esp_wifi_scan_start(&scan, true);
    if (scan_result != ESP_OK) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not scan for Wi-Fi networks");

    wifi_ap_record_t records[16] = {0};
    uint16_t record_count = 16;
    const esp_err_t records_result = esp_wifi_scan_get_ap_records(&record_count, records);
    if (records_result != ESP_OK) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not read Wi-Fi scan results");

    const size_t capacity = 2304;
    char *json = malloc(capacity);
    if (json == NULL) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Not enough memory for scan results");
    size_t length = strlcpy(json, "{\"networks\":[", capacity);
    int visible_count = 0;
    for (uint16_t index = 0; index < record_count; index++) {
        bool duplicate = false;
        for (uint16_t previous = 0; previous < index; previous++) {
            if (strcmp((const char *)records[index].ssid, (const char *)records[previous].ssid) == 0) { duplicate = true; break; }
        }
        if (duplicate || records[index].ssid[0] == '\0' || strcmp((const char *)records[index].ssid, "Adhancron Setup") == 0) continue;
        const int prefix = snprintf(json + length, capacity - length, "%s{\"ssid\":", visible_count++ ? "," : "");
        if (prefix < 0 || (size_t)prefix >= capacity - length) break;
        length += (size_t)prefix;
        if (!append_json_string(json, capacity, &length, records[index].ssid)) break;
        const int suffix = snprintf(json + length, capacity - length, ",\"secure\":%s,\"signal\":\"%s\"}", records[index].authmode == WIFI_AUTH_OPEN ? "false" : "true", signal_label(records[index].rssi));
        if (suffix < 0 || (size_t)suffix >= capacity - length) break;
        length += (size_t)suffix;
    }
    strlcpy(json + length, "]}", capacity - length);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t result = httpd_resp_sendstr(request, json);
    free(json);
    return result;
}

static esp_err_t cast_devices_handler(httpd_req_t *request) {
    cast_device_t devices[CAST_MAX_DEVICES];
    const size_t device_count = cast_sender_discover(
        devices, CAST_MAX_DEVICES, 3500);
    const size_t capacity = 4096;
    char *json = malloc(capacity);
    if (json == NULL) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
            "Not enough memory for Cast discovery");
    }
    size_t length = strlcpy(json, "{\"devices\":[", capacity);
    for (size_t index = 0; index < device_count; index++) {
        const int prefix = snprintf(json + length, capacity - length,
            "%s{\"id\":", index == 0 ? "" : ",");
        if (prefix < 0 || (size_t)prefix >= capacity - length) break;
        length += prefix;
        if (!append_json_string(json, capacity, &length,
                (const uint8_t *)devices[index].id)) break;
        int written = snprintf(json + length, capacity - length, ",\"name\":");
        if (written < 0 || (size_t)written >= capacity - length) break;
        length += written;
        if (!append_json_string(json, capacity, &length,
                (const uint8_t *)devices[index].name)) break;
        written = snprintf(json + length, capacity - length, ",\"model\":");
        if (written < 0 || (size_t)written >= capacity - length) break;
        length += written;
        if (!append_json_string(json, capacity, &length,
                (const uint8_t *)devices[index].model)) break;
        written = snprintf(json + length, capacity - length,
            ",\"group\":%s,\"host\":\"%s\",\"port\":%u}",
            devices[index].is_group ? "true" : "false",
            devices[index].host, devices[index].port);
        if (written < 0 || (size_t)written >= capacity - length) break;
        length += written;
    }
    if (length + 3 >= capacity) {
        free(json);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
            "Too many Cast devices to display");
    }
    memcpy(json + length, "]}", 3);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t result = httpd_resp_sendstr(request, json);
    free(json);
    return result;
}

static esp_err_t dlna_devices_handler(httpd_req_t *request) {
    dlna_device_t *devices = calloc(DLNA_MAX_DEVICES, sizeof(*devices));
    if (devices == NULL) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
            "Not enough memory for network speaker discovery");
    }
    const size_t device_count = dlna_sender_discover(
        devices, DLNA_MAX_DEVICES, 4000);
    const size_t capacity = 7168;
    char *json = malloc(capacity);
    if (json == NULL) {
        free(devices);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
            "Not enough memory for network speaker results");
    }
    size_t length = strlcpy(json, "{\"devices\":[", capacity);
    for (size_t index = 0; index < device_count; index++) {
        const int prefix = snprintf(json + length, capacity - length,
            "%s{\"id\":", index == 0 ? "" : ",");
        if (prefix < 0 || (size_t)prefix >= capacity - length) break;
        length += prefix;
        if (!append_json_string(json, capacity, &length,
                (const uint8_t *)devices[index].id)) break;
        int written = snprintf(json + length, capacity - length, ",\"name\":");
        if (written < 0 || (size_t)written >= capacity - length) break;
        length += written;
        if (!append_json_string(json, capacity, &length,
                (const uint8_t *)devices[index].name)) break;
        written = snprintf(json + length, capacity - length, ",\"model\":");
        if (written < 0 || (size_t)written >= capacity - length) break;
        length += written;
        if (!append_json_string(json, capacity, &length,
                (const uint8_t *)devices[index].model)) break;
        written = snprintf(json + length, capacity - length, ",\"location\":");
        if (written < 0 || (size_t)written >= capacity - length) break;
        length += written;
        if (!append_json_string(json, capacity, &length,
                (const uint8_t *)devices[index].location)) break;
        if (length + 2 >= capacity) break;
        json[length++] = '}';
        json[length] = '\0';
    }
    free(devices);
    if (length + 3 >= capacity) {
        free(json);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
            "Too many network speakers to display");
    }
    memcpy(json + length, "]}", 3);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t result = httpd_resp_sendstr(request, json);
    free(json);
    return result;
}

static void play_task(void *argument) {
    play_audio((audio_track_t)(intptr_t)argument);
    vTaskDelete(NULL);
}
static esp_err_t play_handler(httpd_req_t *request) {
    const bool takbeer = strstr(request->uri, "takbeer") != NULL;
    bool *available = takbeer ? takbeer_audio_available : adhan_audio_available;
    if (!available || !*available || play_audio == NULL) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
            takbeer ? "No Eid takbeer MP3 in internal storage" :
                "No adhan MP3 in internal storage");
    }
    const audio_track_t track = takbeer
        ? AUDIO_TRACK_EID_TAKBEER : AUDIO_TRACK_ADHAN;
    xTaskCreate(play_task, takbeer ? "play_takbeer" : "play_adhan",
        12288, (void *)(intptr_t)track, 4, NULL);
    return httpd_resp_sendstr(request,
        takbeer ? "Eid takbeer playback is starting." :
            "Adhan playback is starting.");
}

static esp_err_t firmware_update_handler(httpd_req_t *request) {
    if (!firmware_update_request_check()) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "An update check is already underway");
    }
    return httpd_resp_sendstr(request,
        "Checking for a verified update. The clock will restart automatically if one is installed.");
}

static esp_err_t audio_upload_handler(httpd_req_t *request) {
    const bool takbeer = strstr(request->uri, "takbeer") != NULL;
    const char *temporary_path = takbeer
        ? TAKBEER_UPLOAD_PATH : ADHAN_UPLOAD_PATH;
    const char *destination_path = takbeer
        ? TAKBEER_AUDIO_PATH : ADHAN_AUDIO_PATH;
    bool *available = takbeer
        ? takbeer_audio_available : adhan_audio_available;
    if (!storage_mounted || !*storage_mounted) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal audio storage is unavailable");
    if (request->content_len == 0 || request->content_len > 7 * 1024 * 1024) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Send an MP3 file under 7 MB");
    uint64_t free_bytes = 0;
    if (esp_vfs_fat_info(ADHAN_STORAGE_BASE_PATH, NULL, &free_bytes) != ESP_OK) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not read internal storage capacity");
    if ((uint64_t)request->content_len + 64 * 1024 > free_bytes) {
        httpd_resp_set_status(request, "507 Insufficient Storage");
        return httpd_resp_sendstr(request, "The new MP3 is too large to replace the existing recording safely");
    }
    remove(temporary_path);
    FILE *file = fopen(temporary_path, "wb");
    if (file == NULL) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not write internal storage");
    char buffer[1024]; int remaining = request->content_len;
    while (remaining > 0) {
        int count = httpd_req_recv(request, buffer, remaining > sizeof(buffer) ? sizeof(buffer) : remaining);
        if (count <= 0 || fwrite(buffer, 1, count, file) != (size_t)count) {
            fclose(file);
            remove(temporary_path);
            return ESP_FAIL;
        }
        remaining -= count;
    }
    if (fclose(file) != 0) {
        remove(temporary_path);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not finish the MP3 upload");
    }
    remove(destination_path);
    if (rename(temporary_path, destination_path) != 0) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not install the MP3 upload");
    if (available != NULL) *available = true;
    return httpd_resp_sendstr(request,
        takbeer ? "Eid takbeer saved to internal storage." :
            "Adhan saved to internal storage.");
}

static bool send_all(httpd_req_t *request, const char *data, size_t length) {
    while (length > 0) {
        const int sent = httpd_send(request, data, length);
        if (sent <= 0) return false;
        data += sent;
        length -= (size_t)sent;
    }
    return true;
}

static esp_err_t audio_file_handler(httpd_req_t *request) {
    const bool takbeer = strstr(request->uri, "takbeer") != NULL;
    const char *path = takbeer ? TAKBEER_AUDIO_PATH : ADHAN_AUDIO_PATH;
    const char *label = takbeer ? "Eid takbeer" : "Adhan";
    FILE *file = fopen(path, "rb");
    if (file == NULL) return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Audio not found");
    fseek(file, 0, SEEK_END); const long file_size = ftell(file); long start = 0; long end = file_size - 1;
    bool partial = false;
    char range[64] = {0};
    if (httpd_req_get_hdr_value_str(request, "Range", range, sizeof(range)) == ESP_OK) {
        long requested_start = 0;
        long requested_end = 0;
        const int parsed = sscanf(range, "bytes=%ld-%ld", &requested_start, &requested_end);
        if (parsed < 1) { fclose(file); return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid range"); }
        start = requested_start;
        end = parsed == 2 ? requested_end : file_size - 1;
        if (end >= file_size) end = file_size - 1;
        if (start < 0 || start > end) { fclose(file); return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid range"); }
        partial = true;
    }
    const long response_size = end - start + 1;
    char headers[384];
    const int header_length = partial
        ? snprintf(headers, sizeof(headers),
            "HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\n"
            "Content-Length: %ld\r\nContent-Range: bytes %ld-%ld/%ld\r\n"
            "Accept-Ranges: bytes\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
            response_size, start, end, file_size)
        : snprintf(headers, sizeof(headers),
            "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
            "Content-Length: %ld\r\nAccept-Ranges: bytes\r\n"
            "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
            response_size);
    if (header_length <= 0 || (size_t)header_length >= sizeof(headers) ||
            !send_all(request, headers, (size_t)header_length)) {
        fclose(file);
        return ESP_FAIL;
    }

    wifi_ps_type_t previous_power_save = WIFI_PS_MIN_MODEM;
    esp_wifi_get_ps(&previous_power_save);
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "Serving %s audio bytes %ld-%ld of %ld",
        label, start, end, file_size);
    fseek(file, start, SEEK_SET);
    long remaining = response_size;
    char *buffer = malloc(16 * 1024);
    if (buffer == NULL) {
        esp_wifi_set_ps(previous_power_save);
        fclose(file);
        return ESP_FAIL;
    }
    while (remaining > 0) {
        const size_t wanted = remaining > 16 * 1024 ? 16 * 1024 : (size_t)remaining;
        const size_t count = fread(buffer, 1, wanted, file);
        if (count == 0 || !send_all(request, buffer, count)) {
            ESP_LOGW(TAG, "%s audio client disconnected with %ld bytes remaining",
                label, remaining);
            break;
        }
        remaining -= (long)count;
    }
    free(buffer);
    esp_wifi_set_ps(previous_power_save);
    fclose(file);
    ESP_LOGI(TAG, "%s audio response complete (%ld bytes sent)",
        label, response_size - remaining);
    return remaining == 0 ? ESP_OK : ESP_FAIL;
}

void web_server_start(adhan_settings_t *settings, bool *storage_is_mounted, bool *adhan_available, bool *takbeer_available, web_play_callback_t play_callback) {
    current_settings = settings; storage_mounted = storage_is_mounted; adhan_audio_available = adhan_available; takbeer_audio_available = takbeer_available; play_audio = play_callback;
    httpd_handle_t server = NULL; httpd_config_t config = HTTPD_DEFAULT_CONFIG(); config.max_uri_handlers = 18; config.stack_size = 12288;
    if (httpd_start(&server, &config) != ESP_OK) { ESP_LOGE(TAG, "Could not start web server"); return; }
    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = page_handler};
    const httpd_uri_t save = {.uri = "/api/settings", .method = HTTP_POST, .handler = settings_handler};
    const httpd_uri_t play = {.uri = "/api/play", .method = HTTP_POST, .handler = play_handler};
    const httpd_uri_t play_takbeer = {.uri = "/api/play/takbeer", .method = HTTP_POST, .handler = play_handler};
    const httpd_uri_t upload = {.uri = "/api/audio", .method = HTTP_POST, .handler = audio_upload_handler};
    const httpd_uri_t upload_takbeer = {.uri = "/api/audio/takbeer", .method = HTTP_POST, .handler = audio_upload_handler};
    const httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler};
    const httpd_uri_t location = {.uri = "/location", .method = HTTP_GET, .handler = location_page_handler};
    const httpd_uri_t wifi = {.uri = "/wifi", .method = HTTP_GET, .handler = wifi_page_handler};
    const httpd_uri_t audio_file = {.uri = "/audio/adhan.mp3", .method = HTTP_GET, .handler = audio_file_handler};
    const httpd_uri_t takbeer_file = {.uri = "/audio/takbeer.mp3", .method = HTTP_GET, .handler = audio_file_handler};
    const httpd_uri_t networks = {.uri = "/api/networks", .method = HTTP_GET, .handler = networks_handler};
    const httpd_uri_t cast_devices = {.uri = "/api/cast-devices", .method = HTTP_GET, .handler = cast_devices_handler};
    const httpd_uri_t dlna_devices = {.uri = "/api/dlna-devices", .method = HTTP_GET, .handler = dlna_devices_handler};
    const httpd_uri_t firmware_update = {.uri = "/api/update", .method = HTTP_POST, .handler = firmware_update_handler};
    const httpd_uri_t stylesheet = {.uri = "/ui.css", .method = HTTP_GET, .handler = stylesheet_handler};
    const httpd_uri_t script = {.uri = "/ui.js", .method = HTTP_GET, .handler = script_handler};
    httpd_register_uri_handler(server, &root); httpd_register_uri_handler(server, &save); httpd_register_uri_handler(server, &play); httpd_register_uri_handler(server, &play_takbeer); httpd_register_uri_handler(server, &upload); httpd_register_uri_handler(server, &upload_takbeer); httpd_register_uri_handler(server, &status); httpd_register_uri_handler(server, &location); httpd_register_uri_handler(server, &wifi); httpd_register_uri_handler(server, &audio_file); httpd_register_uri_handler(server, &takbeer_file); httpd_register_uri_handler(server, &networks); httpd_register_uri_handler(server, &cast_devices); httpd_register_uri_handler(server, &dlna_devices); httpd_register_uri_handler(server, &firmware_update); httpd_register_uri_handler(server, &stylesheet); httpd_register_uri_handler(server, &script);
    ESP_LOGI(TAG, "Web setup interface is ready");
}
