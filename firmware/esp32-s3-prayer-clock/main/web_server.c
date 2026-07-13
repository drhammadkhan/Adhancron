#include "web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "prayer_times.h"

static const char *TAG = "adhan_web";
static adhan_settings_t *current_settings;
static bool *card_mounted;
static bool *audio_available;
static web_play_callback_t play_audio;

static const char STYLE[] =
    "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<style>body{font:16px system-ui;margin:0;background:#f3f6f2;color:#172b26}main{max-width:680px;margin:auto;padding:24px}"
    "h1{margin-bottom:4px}fieldset{border:1px solid #b9c8bf;border-radius:8px;margin:18px 0;padding:16px}"
    "label{display:block;margin:12px 0 4px}input,select,button{box-sizing:border-box;width:100%;padding:11px;font:inherit;border-radius:6px;border:1px solid #9caaa2}"
    "button{background:#176b52;color:white;font-weight:700;border:0;margin-top:18px}.small{color:#52635a;font-size:.9rem}.check{display:flex;gap:8px}.check input{width:auto}</style></head><body><main>";

static const char WIFI_PAGE[] =
    "<h1>Welcome to Adhancron</h1><p class=small>First, connect this prayer clock to your home Wi-Fi. You will choose its location after it is online.</p>"
    "<form method=post action='/api/settings'><input type=hidden name=step value=wifi><fieldset><legend>Home Wi-Fi</legend>"
    "<label>Network</label><select id=ssid name=ssid required><option value=''>Scanning for networks...</option></select><button type=button onclick=scanNetworks()>Scan again</button>"
    "<label>Wi-Fi password</label><input type=password name=password autocomplete='current-password'><p class=small>Leave this blank only for an open network.</p>"
    "<details><summary>Network not listed?</summary><label>Enter a hidden network name</label><input id=manual autocomplete='username' oninput=useManual(this.value)></details></fieldset>"
    "<button>Connect and continue</button></form><script>function useManual(v){let s=document.getElementById('ssid'),o=document.getElementById('manualOption');if(!o){o=document.createElement('option');o.id='manualOption';s.appendChild(o)}o.value=v;o.textContent=v||'Hidden network';if(v)o.selected=true}async function scanNetworks(){let s=document.getElementById('ssid');s.innerHTML='<option value=\"\">Scanning...</option>';try{let r=await fetch('/api/networks',{cache:'no-store'});if(!r.ok)throw Error(await r.text());let j=await r.json();s.innerHTML='<option value=\"\">Choose your home network</option>';j.networks.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' - '+(n.secure?'secured':'open')+' - '+n.signal;s.appendChild(o)});if(!j.networks.length)throw Error('No networks found')}catch(e){s.innerHTML='<option value=\"\">Scan unavailable</option>';document.getElementById('manual').focus()}}scanNetworks()</script></main></body></html>";

static const char LOCATION_PAGE[] =
    "<h1>Set your location</h1><p class=small>Search for your town, city, or postcode. Adhancron obtains the coordinates and timezone online and applies its established prayer-time rules.</p>"
    "<form method=post action='/api/settings'><input type=hidden name=step value=location><fieldset><legend>Home location</legend>"
    "<label>Town, city, or postcode</label><input id=place autocomplete=postal-code required><button type=button onclick=findPlace()>Find location</button><p id=result class=small></p>"
    "<input id=latitude name=latitude type=hidden><input id=longitude name=longitude type=hidden><input id=timezone name=timezone type=hidden><input id=found name=found type=hidden></fieldset>"
    "<fieldset><legend>Playback</legend><label>Speaker volume (0-100)</label><input name=volume type=number min=0 max=100 value='%d'></fieldset><button id=save disabled>Save location</button></form>"
    "<script>async function findPlace(){let q=document.getElementById('place').value,r=document.getElementById('result');r.textContent='Searching...';document.getElementById('save').disabled=true;try{let x=await fetch('https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name='+encodeURIComponent(q));if(!x.ok)throw Error('Location search is unavailable');let j=await x.json(),p=j.results&&j.results[0];if(!p)throw Error('Location not found');document.getElementById('latitude').value=p.latitude;document.getElementById('longitude').value=p.longitude;document.getElementById('timezone').value=p.timezone||'';document.getElementById('found').value='1';document.getElementById('save').disabled=false;r.textContent=[p.name,p.admin1,p.country].filter(Boolean).join(', ')}catch(e){r.textContent=e.message}}</script></main></body></html>";

static const char DASHBOARD_PAGE[] =
    "<h1>Adhancron Prayer Clock</h1><p id=summary class=small>Loading today&apos;s timetable...</p><div id=times></div>"
    "<form method=post action='/api/play'><button type=submit>Play Adhan Now</button></form>"
    "<form method=post action='/api/settings'><input type=hidden name=step value=playback><fieldset><legend>Automatic adhan</legend>"
    "<label class=check><input type=checkbox name=fajr %s>Fajr</label><label class=check><input type=checkbox name=dhuhr %s>Dhuhr</label><label class=check><input type=checkbox name=asr %s>Asr</label><label class=check><input type=checkbox name=maghrib %s>Maghrib</label><label class=check><input type=checkbox name=isha %s>Isha</label>"
    "<label>Speaker volume</label><input name=volume type=number min=0 max=100 value='%d'><button>Save playback settings</button></fieldset></form>"
    "<fieldset><legend>Adhan audio</legend><input id=audio type=file accept='audio/mpeg,.mp3'><button type=button onclick=uploadAudio()>Upload MP3</button><p id=upload class=small></p></fieldset>"
    "<p class=small><a href='/location'>Change location</a> · <a href='/wifi'>Change Wi-Fi</a></p>"
    "<script>async function refresh(){let s=await fetch('/api/status').then(r=>r.json());summary.textContent=s.message;times.innerHTML=s.prayers.map(p=>'<p><b>'+p.name+'</b> '+p.time+'</p>').join('')}"
    "async function uploadAudio(){let f=audio.files[0];if(!f)return;upload.textContent='Uploading...';let r=await fetch('/api/audio',{method:'POST',body:f});upload.textContent=await r.text()}refresh();setInterval(refresh,30000)</script></main></body></html>";

static void restart_task(void *unused) {
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

typedef enum { PAGE_AUTOMATIC, PAGE_LOCATION, PAGE_WIFI } page_kind_t;

static esp_err_t render_page(httpd_req_t *request, page_kind_t kind) {
    const size_t page_capacity = 7000;
    char *page = malloc(page_capacity);
    if (page == NULL) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Not enough memory to render the page");
    int length = snprintf(page, page_capacity, "%s", STYLE);
    if (kind == PAGE_WIFI || (kind == PAGE_AUTOMATIC && !settings_has_wifi(current_settings))) {
        length += snprintf(page + length, page_capacity - length, "%s", WIFI_PAGE);
    } else if (kind == PAGE_LOCATION || (kind == PAGE_AUTOMATIC && !current_settings->location_configured)) {
        length += snprintf(page + length, page_capacity - length, LOCATION_PAGE, current_settings->volume);
    } else {
        length += snprintf(page + length, page_capacity - length, DASHBOARD_PAGE,
            current_settings->enabled[0]?"checked":"",current_settings->enabled[1]?"checked":"",current_settings->enabled[2]?"checked":"",current_settings->enabled[3]?"checked":"",current_settings->enabled[4]?"checked":"",current_settings->volume);
    }
    if (length < 0 || (size_t)length >= page_capacity) {
        free(page);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Page is too large");
    }
    httpd_resp_set_type(request, "text/html");
    const esp_err_t result = httpd_resp_send(request, page, length);
    free(page);
    return result;
}

static esp_err_t page_handler(httpd_req_t *request) { return render_page(request, PAGE_AUTOMATIC); }
static esp_err_t location_page_handler(httpd_req_t *request) { return render_page(request, PAGE_LOCATION); }
static esp_err_t wifi_page_handler(httpd_req_t *request) { return render_page(request, PAGE_WIFI); }

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

static esp_err_t settings_handler(httpd_req_t *request) {
    if (request->content_len > 1536) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Settings are too large");
    }
    char body[1537] = {0};
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
        get_value(body, "latitude", value, sizeof(value)); char *latitude_end = NULL; const double latitude = strtod(value, &latitude_end);
        get_value(body, "longitude", value, sizeof(value)); char *longitude_end = NULL; const double longitude = strtod(value, &longitude_end);
        if (latitude_end == NULL || *latitude_end != '\0' || longitude_end == NULL || *longitude_end != '\0' || latitude < -89.0 || latitude > 89.0 || longitude < -180.0 || longitude > 180.0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "The selected location is invalid");
        current_settings->latitude = latitude; current_settings->longitude = longitude;
        get_value(body, "timezone", value, sizeof(value)); timezone_from_name(current_settings, value);
        current_settings->location_configured = true;
        get_value(body, "volume", value, sizeof(value)); current_settings->volume = atoi(value);
    } else if (strcmp(value, "playback") == 0) {
        const char *keys[] = {"fajr","dhuhr","asr","maghrib","isha"};
        for (int index=0;index<5;index++) { get_value(body,keys[index],value,sizeof(value)); current_settings->enabled[index]=value[0]!='\0'; }
        get_value(body, "volume", value, sizeof(value)); current_settings->volume = atoi(value);
    } else return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unknown setup step");
    current_settings->volume = current_settings->volume < 0 ? 0 : (current_settings->volume > 100 ? 100 : current_settings->volume);
    if (!settings_save(current_settings)) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save settings");
    httpd_resp_sendstr(request, wifi_step
        ? "Wi-Fi saved. The prayer clock is restarting and will connect to your network."
        : "Settings saved. The prayer clock is restarting.");
    xTaskCreate(restart_task, "restart", 2048, NULL, 2, NULL);
    return ESP_OK;
}

static void format_minutes(int minutes, char output[6]) { unsigned value=(unsigned)(((minutes%1440)+1440)%1440); snprintf(output, 6, "%02u:%02u", value/60, value%60); }

static esp_err_t status_handler(httpd_req_t *request) {
    char json[1024];
    const time_t now = time(NULL); struct tm local_now = {0}; localtime_r(&now, &local_now);
    prayer_times_t times; prayer_calculation_config_t config = {.latitude = current_settings->latitude, .longitude = current_settings->longitude};
    if (!current_settings->location_configured || local_now.tm_year < 120 || !prayer_times_calculate(&local_now, &config, &times)) {
        snprintf(json, sizeof(json), "{\"message\":\"Waiting for location and clock sync\",\"prayers\":[]}");
    } else {
        char values[6][6]; for (int i = 0; i < 6; i++) format_minutes(prayer_time_minutes(&times, i), values[i]);
        const char *storage = card_mounted && *card_mounted ? (audio_available && *audio_available ? "Adhan audio ready" : "Upload an adhan MP3") : "SD card missing";
        snprintf(json, sizeof(json), "{\"message\":\"%04d-%02d-%02d %02d:%02d · %s\",\"prayers\":[{\"name\":\"Fajr\",\"time\":\"%s\"},{\"name\":\"Sunrise\",\"time\":\"%s\"},{\"name\":\"Dhuhr\",\"time\":\"%s\"},{\"name\":\"Asr\",\"time\":\"%s\"},{\"name\":\"Maghrib\",\"time\":\"%s\"},{\"name\":\"Isha\",\"time\":\"%s\"}]}", local_now.tm_year+1900,local_now.tm_mon+1,local_now.tm_mday,local_now.tm_hour,local_now.tm_min,storage,values[0],values[1],values[2],values[3],values[4],values[5]);
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

static void play_task(void *unused) { play_audio(); vTaskDelete(NULL); }
static esp_err_t play_handler(httpd_req_t *request) {
    if (!audio_available || !*audio_available || play_audio == NULL) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "No adhan.mp3 on the SD card");
    xTaskCreate(play_task, "play_adhan", 8192, NULL, 4, NULL);
    return httpd_resp_sendstr(request, "Adhan started.");
}

static esp_err_t audio_upload_handler(httpd_req_t *request) {
    if (!card_mounted || !*card_mounted || request->content_len == 0 || request->content_len > 8 * 1024 * 1024) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Insert an SD card and send an MP3 file under 8 MB");
    FILE *file = fopen("/sdcard/adhan.upload", "wb");
    if (file == NULL) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not write SD card");
    char buffer[1024]; int remaining = request->content_len;
    while (remaining > 0) { int count = httpd_req_recv(request, buffer, remaining > sizeof(buffer) ? sizeof(buffer) : remaining); if (count <= 0) { fclose(file); return ESP_FAIL; } fwrite(buffer, 1, count, file); remaining -= count; }
    if (fclose(file) != 0) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not finish the MP3 upload");
    remove("/sdcard/adhan.mp3");
    if (rename("/sdcard/adhan.upload", "/sdcard/adhan.mp3") != 0) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not install the MP3 upload");
    *audio_available = true;
    return httpd_resp_sendstr(request, "Audio uploaded.");
}

static esp_err_t audio_file_handler(httpd_req_t *request) {
    FILE *file = fopen("/sdcard/adhan.mp3", "rb");
    if (file == NULL) return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Audio not found");
    fseek(file, 0, SEEK_END); const long file_size = ftell(file); long start = 0; long end = file_size - 1;
    char range[64] = {0};
    if (httpd_req_get_hdr_value_str(request, "Range", range, sizeof(range)) == ESP_OK && sscanf(range, "bytes=%ld-%ld", &start, &end) >= 1) {
        if (end <= 0 || end >= file_size) end = file_size - 1;
        if (start < 0 || start > end) { fclose(file); return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid range"); }
        char content_range[80]; snprintf(content_range, sizeof(content_range), "bytes %ld-%ld/%ld", start, end, file_size);
        httpd_resp_set_status(request, "206 Partial Content"); httpd_resp_set_hdr(request, "Content-Range", content_range);
    }
    httpd_resp_set_type(request, "audio/mpeg"); httpd_resp_set_hdr(request, "Accept-Ranges", "bytes");
    fseek(file, start, SEEK_SET); long remaining = end - start + 1; char buffer[2048];
    while (remaining > 0) { size_t count = fread(buffer, 1, remaining > sizeof(buffer) ? sizeof(buffer) : remaining, file); if (count == 0) break; if (httpd_resp_send_chunk(request, buffer, count) != ESP_OK) { fclose(file); return ESP_FAIL; } remaining -= count; }
    fclose(file); return httpd_resp_send_chunk(request, NULL, 0);
}

void web_server_start(adhan_settings_t *settings, bool *sd_is_mounted, bool *adhan_audio_available, web_play_callback_t play_callback) {
    current_settings = settings; card_mounted = sd_is_mounted; audio_available = adhan_audio_available; play_audio = play_callback;
    httpd_handle_t server = NULL; httpd_config_t config = HTTPD_DEFAULT_CONFIG(); config.max_uri_handlers = 9; config.stack_size = 8192;
    if (httpd_start(&server, &config) != ESP_OK) { ESP_LOGE(TAG, "Could not start web server"); return; }
    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = page_handler};
    const httpd_uri_t save = {.uri = "/api/settings", .method = HTTP_POST, .handler = settings_handler};
    const httpd_uri_t play = {.uri = "/api/play", .method = HTTP_POST, .handler = play_handler};
    const httpd_uri_t upload = {.uri = "/api/audio", .method = HTTP_POST, .handler = audio_upload_handler};
    const httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler};
    const httpd_uri_t location = {.uri = "/location", .method = HTTP_GET, .handler = location_page_handler};
    const httpd_uri_t wifi = {.uri = "/wifi", .method = HTTP_GET, .handler = wifi_page_handler};
    const httpd_uri_t audio_file = {.uri = "/audio/adhan.mp3", .method = HTTP_GET, .handler = audio_file_handler};
    const httpd_uri_t networks = {.uri = "/api/networks", .method = HTTP_GET, .handler = networks_handler};
    httpd_register_uri_handler(server, &root); httpd_register_uri_handler(server, &save); httpd_register_uri_handler(server, &play); httpd_register_uri_handler(server, &upload); httpd_register_uri_handler(server, &status); httpd_register_uri_handler(server, &location); httpd_register_uri_handler(server, &wifi); httpd_register_uri_handler(server, &audio_file); httpd_register_uri_handler(server, &networks);
    ESP_LOGI(TAG, "Web setup interface is ready");
}
