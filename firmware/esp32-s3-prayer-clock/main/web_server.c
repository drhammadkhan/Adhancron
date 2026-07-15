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

static const char STYLE[] =
    "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<style>body{font:16px system-ui;margin:0;background:#f3f6f2;color:#172b26}main{max-width:680px;margin:auto;padding:24px}"
    "h1{margin-bottom:4px}fieldset{border:1px solid #b9c8bf;border-radius:8px;margin:18px 0;padding:16px}"
    "label{display:block;margin:12px 0 4px}input,select,button{box-sizing:border-box;width:100%;padding:11px;font:inherit;border-radius:6px;border:1px solid #9caaa2}"
    "button{background:#176b52;color:white;font-weight:700;border:0;margin-top:18px}.small{color:#52635a;font-size:.9rem}.check{display:flex;gap:8px;align-items:center}.check input{width:auto}.choice{padding:9px 0}.hidden{display:none}</style></head><body><main>";

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
    "<label>Town, city, or UK postcode</label><input id=place autocomplete=postal-code><button type=button onclick=findPlace()>Find location</button><p id=result class=small></p>"
    "<details><summary>Advanced location settings</summary><p class=small>Use decimal coordinates if the location search cannot find your home.</p>"
    "<label>Latitude</label><input id=latitude name=latitude type=number min=-89 max=89 step=any placeholder='51.5074' oninput=manualChanged()>"
    "<label>Longitude</label><input id=longitude name=longitude type=number min=-180 max=180 step=any placeholder='-0.1278' oninput=manualChanged()>"
    "<button type=button onclick=useCoordinates()>Use these coordinates</button></details>"
    "<input id=timezone name=timezone type=hidden><input id=location_name name=location_name type=hidden><input id=found name=found type=hidden></fieldset>"
    "<fieldset><legend>Playback</legend><label>Speaker volume (0-100)</label><input name=volume type=number min=0 max=100 value='%d'></fieldset><button id=save disabled>Save location</button></form>"
    "<script>function usePlace(p){let label=[p.name,p.admin1,p.country].filter(Boolean).join(', ');document.getElementById('latitude').value=p.latitude;document.getElementById('longitude').value=p.longitude;document.getElementById('timezone').value=p.timezone||'';document.getElementById('location_name').value=p.location_name||label;document.getElementById('found').value='1';document.getElementById('save').disabled=false;document.getElementById('result').textContent=p.message||label}"
    "async function findPlace(){let q=document.getElementById('place').value.trim(),r=document.getElementById('result'),postcode=q.toUpperCase().replace(/\\s+/g,'');r.textContent='Searching...';document.getElementById('save').disabled=true;document.getElementById('found').value='';if(!q){r.textContent='Enter a town, city, or UK postcode.';return}try{if(/^[A-Z]{1,2}\\d[A-Z\\d]?\\d[A-Z]{2}$/.test(postcode)){let x=await fetch('https://api.postcodes.io/postcodes/'+encodeURIComponent(postcode));if(x.ok){let j=await x.json(),p=j.result;if(p){usePlace({latitude:p.latitude,longitude:p.longitude,timezone:'Europe/London',name:p.postcode,admin1:p.admin_district||p.region,country:p.country});return}}}let x=await fetch('https://geocoding-api.open-meteo.com/v1/search?count=5&language=en&format=json&name='+encodeURIComponent(q));if(!x.ok)throw Error('Location search is temporarily unavailable.');let j=await x.json(),p=j.results&&j.results[0];if(!p)throw Error('Location not found. Check the postcode or try your nearest town.');usePlace(p)}catch(e){r.textContent=e.message}}"
    "function manualChanged(){document.getElementById('found').value='';document.getElementById('timezone').value='';document.getElementById('location_name').value='';document.getElementById('save').disabled=true;document.getElementById('result').textContent='Select Use these coordinates to confirm them.'}"
    "async function useCoordinates(){let a=document.getElementById('latitude'),o=document.getElementById('longitude'),lat=Number(a.value),lon=Number(o.value),r=document.getElementById('result');if(a.value===''||o.value===''||!Number.isFinite(lat)||!Number.isFinite(lon)||lat< -89||lat>89||lon< -180||lon>180){r.textContent='Enter a latitude from -89 to 89 and longitude from -180 to 180.';return}r.textContent='Checking coordinates...';let zone='';try{let x=await fetch('https://api.open-meteo.com/v1/forecast?latitude='+lat+'&longitude='+lon+'&timezone=auto&forecast_days=1');if(x.ok){let j=await x.json();zone=j.timezone||''}}catch(e){}let coordinates=lat.toFixed(4)+', '+lon.toFixed(4);usePlace({latitude:lat,longitude:lon,timezone:zone,name:coordinates,location_name:coordinates,message:coordinates+' - '+(zone||'timezone estimated')})}"
    "document.getElementById('place').addEventListener('keydown',function(e){if(e.key==='Enter'){e.preventDefault();findPlace()}})</script></main></body></html>";

static const char DASHBOARD_PAGE[] =
    "<h1>Adhancron Prayer Clock</h1><p id=place-name><b>Loading saved location...</b></p><p id=summary class=small>Loading today&apos;s timetable...</p><p id=battery-status class=small></p><div id=times></div>"
    "<button type=button onclick=\"testPlayback('adhan')\">Play Adhan Now</button><button type=button onclick=\"testPlayback('takbeer')\">Play Eid Takbeer Now</button><p id=test-result class=small></p>"
    "<form method=post action='/api/settings'><input type=hidden name=step value=playback>"
    "<fieldset><legend>Playback</legend><label class='check choice'><input type=radio name=output value=attached %s onchange=outputChanged()>Attached speaker</label>"
    "<label class='check choice'><input type=radio name=output value=cast %s onchange=outputChanged()>Google Cast speaker</label>"
    "<div id=cast-controls><label>Cast speaker</label><select id=cast-device name=cast_device_id onchange=castSelectionChanged()><option value=''>Scanning for speakers...</option></select>"
    "<input id=cast-name name=cast_device_name type=hidden><button type=button onclick=scanCastDevices()>Scan again</button><p id=cast-result class=small></p></div>"
    "<label>Playback volume</label><input name=volume type=number min=0 max=100 value='%d'></fieldset>"
    "<fieldset><legend>Automatic adhan</legend>"
    "<label class=check><input type=checkbox name=fajr %s>Fajr</label><label class=check><input type=checkbox name=dhuhr %s>Dhuhr</label><label class=check><input type=checkbox name=asr %s>Asr</label><label class=check><input type=checkbox name=maghrib %s>Maghrib</label><label class=check><input type=checkbox name=isha %s>Isha</label>"
    "</fieldset><fieldset><legend>Ramadan</legend><p class=small>Choose the first and final fasting day for your community. Both dates are inclusive and should make a 29- or 30-day period.</p>"
    "<label>First fasting day</label><input id=ramadan-start name=ramadan_start type=date value='%s'>"
    "<label>Final fasting day</label><input id=ramadan-end name=ramadan_end type=date value='%s'>"
    "<p id=ramadan-status class=small>Loading Ramadan status...</p><button type=button onclick=clearRamadan()>Turn Ramadan mode off</button>"
    "</fieldset><fieldset><legend>Eid days and takbeer</legend><p class=small>Set the locally observed dates for Eid al-Fitr and Eid al-Adha. On either date, the clock shows an Eid greeting and plays the separate takbeer recording repeatedly during this window.</p>"
    "<label>Eid al-Fitr date</label><input id=eid-fitr name=eid_fitr type=date value='%s'>"
    "<label>Eid al-Adha date</label><input id=eid-adha name=eid_adha type=date value='%s'>"
    "<label>Takbeer starts</label><input name=eid_takbeer_start type=time value='%s' required>"
    "<label>Takbeer finishes</label><input name=eid_takbeer_end type=time value='%s' required>"
    "<label>Repeat every (minutes)</label><input name=eid_takbeer_interval type=number min=5 max=120 step=5 value='%d' required>"
    "<p id=eid-status class=small>Loading Eid status...</p><button type=button onclick=clearEid()>Clear Eid dates</button>"
    "</fieldset><fieldset><legend>Software updates</legend><p id=firmware-status class=small>Loading firmware status...</p>"
    "<label class=check><input type=checkbox name=automatic_updates %s>Install verified updates automatically over Wi-Fi</label>"
    "<button id=update-button type=button onclick=checkUpdate()>Check and install now</button><p id=update-result class=small></p></fieldset>"
    "<button>Save settings</button></form>"
    "<fieldset><legend>Audio recordings</legend><p id=adhan-audio-status class=small></p><label>Adhan MP3</label><input id=adhan-audio type=file accept='audio/mpeg,.mp3'><button type=button onclick=\"uploadAudio('adhan-audio','/api/audio','adhan-upload')\">Upload adhan MP3</button><p id=adhan-upload class=small></p>"
    "<p id=takbeer-audio-status class=small></p><label>Eid takbeer MP3</label><input id=takbeer-audio type=file accept='audio/mpeg,.mp3'><button type=button onclick=\"uploadAudio('takbeer-audio','/api/audio/takbeer','takbeer-upload')\">Upload takbeer MP3</button><p id=takbeer-upload class=small></p></fieldset>"
    "<p class=small><a href='/location'>Change location</a> &middot; <a href='/wifi'>Change Wi-Fi</a></p>"
    "<script>let savedCastId='',savedCastName='';async function refresh(){let s=await fetch('/api/status',{cache:'no-store'}).then(r=>r.json());savedCastId=s.cast_device_id||'';savedCastName=s.cast_device_name||'';document.getElementById('place-name').textContent=s.location?'Prayer times for '+s.location:'Location not configured';summary.textContent=s.message;let b=document.getElementById('battery-status');b.textContent=s.battery_available?'Battery '+s.battery_percentage+'%% - '+(s.battery_millivolts/1000).toFixed(2)+' V - '+(s.battery_charging?'charging detected':s.battery_full?'fully charged':'battery connected'):'';times.innerHTML=s.prayers.map(p=>'<p><b>'+p.name+'</b> '+p.time+'</p>').join('');document.getElementById('ramadan-status').textContent=s.ramadan_message||'Ramadan status unavailable';document.getElementById('eid-status').textContent=s.eid_message||'Eid status unavailable';document.getElementById('adhan-audio-status').textContent=s.adhan_audio_available?'Adhan recording ready.':'No adhan recording saved.';document.getElementById('takbeer-audio-status').textContent=s.takbeer_audio_available?'Eid takbeer recording ready.':'Upload an Eid takbeer recording before the configured date.';document.getElementById('firmware-status').textContent='Version '+(s.firmware_version||'unknown')+' - '+(s.update_status||'Update status unavailable');document.getElementById('update-button').disabled=!!s.update_running}"
    "function outputChanged(){let cast=document.querySelector('input[name=output]:checked').value==='cast';document.getElementById('cast-controls').classList.toggle('hidden',!cast);document.getElementById('cast-device').disabled=!cast;document.getElementById('cast-name').disabled=!cast}"
    "function castSelectionChanged(){let s=document.getElementById('cast-device'),o=s.options[s.selectedIndex];document.getElementById('cast-name').value=o&&o.dataset.name||''}"
    "async function scanCastDevices(){let s=document.getElementById('cast-device'),r=document.getElementById('cast-result');s.innerHTML='<option value=\"\">Scanning...</option>';r.textContent='Looking for speakers on this network...';try{let x=await fetch('/api/cast-devices',{cache:'no-store'});if(!x.ok)throw Error(await x.text());let j=await x.json();s.innerHTML='<option value=\"\">Choose a speaker</option>';j.devices.forEach(d=>{let o=document.createElement('option');o.value=d.id;o.dataset.name=d.name;o.textContent=d.name+(d.group?' (speaker group)':'')+(d.model?' - '+d.model:'');if(d.id===savedCastId)o.selected=true;s.appendChild(o)});if(savedCastId&&!j.devices.some(d=>d.id===savedCastId)){let o=document.createElement('option');o.value=savedCastId;o.dataset.name=savedCastName;o.textContent=savedCastName+' (currently unavailable)';o.selected=true;s.appendChild(o)}castSelectionChanged();r.textContent=j.devices.length?j.devices.length+' Cast speaker'+(j.devices.length===1?'':'s')+' found.':'No Cast speakers found.'}catch(e){r.textContent=e.message}}"
    "async function testPlayback(track){let r=document.getElementById('test-result');r.textContent='Starting playback...';try{let x=await fetch(track==='takbeer'?'/api/play/takbeer':'/api/play',{method:'POST'});r.textContent=await x.text()}catch(e){r.textContent=e.message}}"
    "async function checkUpdate(){let b=document.getElementById('update-button'),r=document.getElementById('update-result');b.disabled=true;r.textContent='Starting update check...';try{let x=await fetch('/api/update',{method:'POST'});r.textContent=await x.text()}catch(e){r.textContent=e.message}setTimeout(refresh,1000)}"
    "function clearRamadan(){document.getElementById('ramadan-start').value='';document.getElementById('ramadan-end').value='';document.getElementById('ramadan-status').textContent='Save settings to turn Ramadan mode off.'}"
    "function clearEid(){document.getElementById('eid-fitr').value='';document.getElementById('eid-adha').value='';document.getElementById('eid-status').textContent='Save settings to clear both Eid dates.'}"
    "async function uploadAudio(inputId,endpoint,resultId){let f=document.getElementById(inputId).files[0],r=document.getElementById(resultId);if(!f)return;r.textContent='Uploading...';let x=await fetch(endpoint,{method:'POST',body:f});r.textContent=await x.text();await refresh()}async function init(){await refresh();outputChanged();await scanCastDevices()}init();setInterval(refresh,30000)</script></main></body></html>";

static void restart_task(void *unused) {
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

typedef enum { PAGE_AUTOMATIC, PAGE_LOCATION, PAGE_WIFI } page_kind_t;

static void format_minutes(int minutes, char output[6]);

static esp_err_t render_page(httpd_req_t *request, page_kind_t kind) {
    const size_t page_capacity = 20000;
    char *page = malloc(page_capacity);
    if (page == NULL) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Not enough memory to render the page");
    int length = snprintf(page, page_capacity, "%s", STYLE);
    if (kind == PAGE_WIFI || (kind == PAGE_AUTOMATIC && !settings_has_wifi(current_settings))) {
        length += snprintf(page + length, page_capacity - length, "%s", WIFI_PAGE);
    } else if (kind == PAGE_LOCATION || (kind == PAGE_AUTOMATIC && !current_settings->location_configured)) {
        length += snprintf(page + length, page_capacity - length, LOCATION_PAGE, current_settings->volume);
    } else {
        char eid_start[6];
        char eid_end[6];
        format_minutes(current_settings->eid_takbeer_start_minute, eid_start);
        format_minutes(current_settings->eid_takbeer_end_minute, eid_end);
        length += snprintf(page + length, page_capacity - length, DASHBOARD_PAGE,
            current_settings->output == ADHAN_OUTPUT_ATTACHED ? "checked" : "",
            current_settings->output == ADHAN_OUTPUT_CAST ? "checked" : "",
            current_settings->volume,
            current_settings->enabled[0]?"checked":"",current_settings->enabled[1]?"checked":"",current_settings->enabled[2]?"checked":"",current_settings->enabled[3]?"checked":"",current_settings->enabled[4]?"checked":"",
            current_settings->ramadan_start_date,
            current_settings->ramadan_end_date,
            current_settings->eid_fitr_date,
            current_settings->eid_adha_date,
            eid_start,
            eid_end,
            current_settings->eid_takbeer_interval_minutes,
            current_settings->automatic_updates ? "checked" : "");
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
            ? ADHAN_OUTPUT_CAST : ADHAN_OUTPUT_ATTACHED;
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
        }
    } else return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unknown setup step");
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
    char json[3200];
    size_t length = strlcpy(json, "{\"location\":", sizeof(json));
    if (!append_json_string(json, sizeof(json), &length,
            (const uint8_t *)current_settings->location_name)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not render status");
    }
    int written = snprintf(json + length, sizeof(json) - length,
        ",\"playback_output\":\"%s\",\"cast_device_id\":",
        current_settings->output == ADHAN_OUTPUT_CAST ? "cast" : "attached");
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
    char eid_start[6];
    char eid_end[6];
    format_minutes(current_settings->eid_takbeer_start_minute, eid_start);
    format_minutes(current_settings->eid_takbeer_end_minute, eid_end);
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
    httpd_handle_t server = NULL; httpd_config_t config = HTTPD_DEFAULT_CONFIG(); config.max_uri_handlers = 16; config.stack_size = 12288;
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
    const httpd_uri_t firmware_update = {.uri = "/api/update", .method = HTTP_POST, .handler = firmware_update_handler};
    httpd_register_uri_handler(server, &root); httpd_register_uri_handler(server, &save); httpd_register_uri_handler(server, &play); httpd_register_uri_handler(server, &play_takbeer); httpd_register_uri_handler(server, &upload); httpd_register_uri_handler(server, &upload_takbeer); httpd_register_uri_handler(server, &status); httpd_register_uri_handler(server, &location); httpd_register_uri_handler(server, &wifi); httpd_register_uri_handler(server, &audio_file); httpd_register_uri_handler(server, &takbeer_file); httpd_register_uri_handler(server, &networks); httpd_register_uri_handler(server, &cast_devices); httpd_register_uri_handler(server, &firmware_update);
    ESP_LOGI(TAG, "Web setup interface is ready");
}
