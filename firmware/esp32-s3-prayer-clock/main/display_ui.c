#include "display_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "board_config.h"
#include "prayer_times.h"

static const char *TAG = "adhan_display";

static const uint32_t COLOR_BACKGROUND = 0x071D1A;
static const uint32_t COLOR_PANEL = 0x123A34;
static const uint32_t COLOR_PANEL_ACTIVE = 0x23483D;
static const uint32_t COLOR_DIVIDER = 0x285049;
static const uint32_t COLOR_IVORY = 0xF7F2E7;
static const uint32_t COLOR_MUTED = 0x8EAAA3;
static const uint32_t COLOR_GOLD = 0xE6B967;
static const uint32_t COLOR_SKY = 0x79C3D2;
static const uint32_t COLOR_ERROR = 0xE58B78;

static lv_display_t *lvgl_display;
static lv_obj_t *dashboard;
static lv_obj_t *message_panel;
static lv_obj_t *message_title;
static lv_obj_t *message_detail;
static lv_obj_t *clock_label;
static lv_obj_t *date_label;
static lv_obj_t *wifi_icon;
static lv_obj_t *audio_icon;
static lv_obj_t *next_name_label;
static lv_obj_t *next_time_label;
static lv_obj_t *countdown_label;
static lv_obj_t *prayer_rows[6];
static lv_obj_t *prayer_name_labels[6];
static lv_obj_t *prayer_time_labels[6];

static lv_color_t color(uint32_t value) {
    return lv_color_hex(value);
}

static void make_plain(lv_obj_t *object) {
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_label(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    uint32_t text_color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color(text_color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static void create_crescent(lv_obj_t *parent) {
    lv_obj_t *holder = lv_obj_create(parent);
    make_plain(holder);
    lv_obj_set_size(holder, 28, 28);
    lv_obj_set_pos(holder, 10, 7);

    lv_obj_t *moon = lv_obj_create(holder);
    make_plain(moon);
    lv_obj_set_size(moon, 22, 22);
    lv_obj_set_pos(moon, 1, 3);
    lv_obj_set_style_radius(moon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(moon, color(COLOR_GOLD), 0);
    lv_obj_set_style_bg_opa(moon, LV_OPA_COVER, 0);

    lv_obj_t *cutout = lv_obj_create(holder);
    make_plain(cutout);
    lv_obj_set_size(cutout, 19, 19);
    lv_obj_set_pos(cutout, 9, 0);
    lv_obj_set_style_radius(cutout, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cutout, color(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(cutout, LV_OPA_COVER, 0);
}

static void create_message_panel(lv_obj_t *screen) {
    message_panel = lv_obj_create(screen);
    make_plain(message_panel);
    lv_obj_set_size(message_panel, 220, 150);
    lv_obj_center(message_panel);
    lv_obj_set_style_radius(message_panel, 8, 0);
    lv_obj_set_style_bg_color(message_panel, color(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(message_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(message_panel, 1, 0);
    lv_obj_set_style_border_color(message_panel, color(COLOR_DIVIDER), 0);
    lv_obj_set_style_pad_all(message_panel, 18, 0);

    lv_obj_t *mark = make_label(message_panel, "ADHANCRON", &lv_font_montserrat_12, COLOR_GOLD);
    lv_obj_align(mark, LV_ALIGN_TOP_LEFT, 0, 0);

    message_title = make_label(message_panel, "Starting", &lv_font_montserrat_20, COLOR_IVORY);
    lv_obj_set_width(message_title, 184);
    lv_label_set_long_mode(message_title, LV_LABEL_LONG_WRAP);
    lv_obj_align(message_title, LV_ALIGN_TOP_LEFT, 0, 34);

    message_detail = make_label(message_panel, "Preparing your prayer clock", &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_width(message_detail, 184);
    lv_label_set_long_mode(message_detail, LV_LABEL_LONG_WRAP);
    lv_obj_align(message_detail, LV_ALIGN_TOP_LEFT, 0, 72);
}

static void create_next_prayer_panel(lv_obj_t *parent) {
    lv_obj_t *panel = lv_obj_create(parent);
    make_plain(panel);
    lv_obj_set_pos(panel, 10, 101);
    lv_obj_set_size(panel, 220, 70);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_bg_color(panel, color(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, color(COLOR_DIVIDER), 0);

    lv_obj_t *eyebrow = make_label(panel, "NEXT PRAYER", &lv_font_montserrat_12, COLOR_GOLD);
    lv_obj_set_pos(eyebrow, 12, 8);

    next_name_label = make_label(panel, "FAJR", &lv_font_montserrat_20, COLOR_IVORY);
    lv_obj_set_pos(next_name_label, 12, 27);

    next_time_label = make_label(panel, "--:--", &lv_font_montserrat_20, COLOR_IVORY);
    lv_obj_align(next_time_label, LV_ALIGN_TOP_RIGHT, -12, 27);

    countdown_label = make_label(panel, "IN --:--", &lv_font_montserrat_12, COLOR_SKY);
    lv_obj_align(countdown_label, LV_ALIGN_BOTTOM_RIGHT, -12, -7);
}

static void create_prayer_table(lv_obj_t *parent) {
    static const char *names[] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
    for (int row = 0; row < 6; row++) {
        prayer_rows[row] = lv_obj_create(parent);
        make_plain(prayer_rows[row]);
        lv_obj_set_pos(prayer_rows[row], 10, 179 + row * 22);
        lv_obj_set_size(prayer_rows[row], 220, 22);
        lv_obj_set_style_radius(prayer_rows[row], 4, 0);
        lv_obj_set_style_bg_opa(prayer_rows[row], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_side(prayer_rows[row], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(prayer_rows[row], row == 5 ? 0 : 1, 0);
        lv_obj_set_style_border_color(prayer_rows[row], color(COLOR_DIVIDER), 0);

        prayer_name_labels[row] = make_label(
            prayer_rows[row], names[row], &lv_font_montserrat_14,
            row == 1 ? COLOR_SKY : COLOR_MUTED);
        lv_obj_align(prayer_name_labels[row], LV_ALIGN_LEFT_MID, 6, 0);

        prayer_time_labels[row] = make_label(
            prayer_rows[row], "--:--", &lv_font_montserrat_14, COLOR_IVORY);
        lv_obj_align(prayer_time_labels[row], LV_ALIGN_RIGHT_MID, -6, 0);
    }
}

static void create_dashboard(lv_obj_t *screen) {
    dashboard = lv_obj_create(screen);
    make_plain(dashboard);
    lv_obj_set_size(dashboard, BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);
    lv_obj_set_style_bg_color(dashboard, color(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(dashboard, LV_OPA_COVER, 0);

    create_crescent(dashboard);
    lv_obj_t *brand = make_label(dashboard, "ADHANCRON", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_set_pos(brand, 44, 13);

    wifi_icon = make_label(dashboard, LV_SYMBOL_WIFI, &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_pos(wifi_icon, 188, 11);
    audio_icon = make_label(dashboard, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_pos(audio_icon, 215, 11);

    clock_label = make_label(dashboard, "--:--", &lv_font_montserrat_48, COLOR_IVORY);
    lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, 34);

    date_label = make_label(dashboard, "WAITING FOR TIME", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 84);

    create_next_prayer_panel(dashboard);
    create_prayer_table(dashboard);
}

esp_err_t display_ui_init(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_handle_t panel) {
    const lvgl_port_cfg_t port_config = {
        .task_priority = 3,
        .task_stack = 7168,
        .task_affinity = -1,
        .task_max_sleep_ms = 100,
        .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_config), TAG, "LVGL port initialisation failed");

    const lvgl_port_display_cfg_t display_config = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .buffer_size = BOARD_LCD_WIDTH * 32,
        .double_buffer = true,
        .hres = BOARD_LCD_WIDTH,
        .vres = BOARD_LCD_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = true,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    lvgl_display = lvgl_port_add_disp(&display_config);
    ESP_RETURN_ON_FALSE(lvgl_display, ESP_ERR_NO_MEM, TAG, "LVGL display registration failed");

    lvgl_port_lock(0);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, color(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    create_dashboard(screen);
    create_message_panel(screen);
    lv_obj_add_flag(dashboard, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "LVGL prayer-clock interface ready");
    return ESP_OK;
}

static void show_message(const char *title, const char *detail, uint32_t title_color) {
    lv_label_set_text(message_title, title);
    lv_obj_set_style_text_color(message_title, color(title_color), 0);
    lv_label_set_text(message_detail, detail);
    lv_obj_clear_flag(message_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(dashboard, LV_OBJ_FLAG_HIDDEN);
}

static void format_clock_minutes(int minutes, char output[6]) {
    const unsigned value = (unsigned)(((minutes % 1440) + 1440) % 1440);
    snprintf(output, 6, "%02u:%02u", value / 60, value % 60);
}

void display_ui_update(
    const adhan_settings_t *current_settings,
    bool connected,
    bool card_mounted,
    bool audio_available) {
    if (!lvgl_display || !current_settings || !lvgl_port_lock(1000)) return;

    if (!settings_has_wifi(current_settings)) {
        show_message("Connect to Wi-Fi", "Join 'Adhancron Setup', then open 192.168.4.1", COLOR_GOLD);
        lvgl_port_unlock();
        return;
    }
    if (!connected) {
        show_message("Connecting", "Trying your saved Wi-Fi network", COLOR_GOLD);
        lvgl_port_unlock();
        return;
    }
    if (!current_settings->location_configured) {
        show_message("Set your location", "Open adhancron.local in a browser", COLOR_GOLD);
        lvgl_port_unlock();
        return;
    }

    const time_t now = time(NULL);
    struct tm local_now = {0};
    localtime_r(&now, &local_now);
    if (local_now.tm_year < 120) {
        show_message("Setting the time", "Synchronising securely over the internet", COLOR_GOLD);
        lvgl_port_unlock();
        return;
    }

    prayer_times_t times;
    const prayer_calculation_config_t calculation = {
        .latitude = current_settings->latitude,
        .longitude = current_settings->longitude,
    };
    if (!prayer_times_calculate(&local_now, &calculation, &times)) {
        show_message("Times unavailable", "Check the saved location in Settings", COLOR_ERROR);
        lvgl_port_unlock();
        return;
    }

    lv_obj_add_flag(message_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(dashboard, LV_OBJ_FLAG_HIDDEN);

    char clock_text[6];
    snprintf(clock_text, sizeof(clock_text), "%02d%c%02d",
        local_now.tm_hour, local_now.tm_sec % 2 == 0 ? ':' : ' ', local_now.tm_min);
    lv_label_set_text(clock_label, clock_text);

    char date_text[32];
    strftime(date_text, sizeof(date_text), "%A  %d %B", &local_now);
    lv_label_set_text(date_label, date_text);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 84);

    lv_obj_set_style_text_color(wifi_icon, color(connected ? COLOR_SKY : COLOR_MUTED), 0);
    lv_obj_set_style_text_color(audio_icon, color(audio_available ? COLOR_GOLD : COLOR_ERROR), 0);
    lv_label_set_text(audio_icon, card_mounted ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_WARNING);

    static const int row_indexes[] = {0, 1, 2, 3, 4, 5};
    static const int prayer_indexes[] = {0, 2, 3, 4, 5};
    static const char *prayer_names[] = {"FAJR", "DHUHR", "ASR", "MAGHRIB", "ISHA"};
    for (int row = 0; row < 6; row++) {
        char value[6];
        format_clock_minutes(prayer_time_minutes(&times, row_indexes[row]), value);
        lv_label_set_text(prayer_time_labels[row], value);
        lv_obj_set_style_bg_opa(prayer_rows[row], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(
            prayer_name_labels[row], color(row == 1 ? COLOR_SKY : COLOR_MUTED), 0);
    }

    const int current_minute = local_now.tm_hour * 60 + local_now.tm_min;
    int next = 0;
    for (int index = 0; index < 5; index++) {
        if (prayer_time_minutes(&times, prayer_indexes[index]) > current_minute) {
            next = index;
            break;
        }
    }
    const int next_row = prayer_indexes[next];
    lv_obj_set_style_bg_color(prayer_rows[next_row], color(COLOR_PANEL_ACTIVE), 0);
    lv_obj_set_style_bg_opa(prayer_rows[next_row], LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(prayer_name_labels[next_row], color(COLOR_GOLD), 0);

    const int next_minutes = prayer_time_minutes(&times, prayer_indexes[next]);
    char next_time[6];
    format_clock_minutes(next_minutes, next_time);
    lv_label_set_text(next_name_label, prayer_names[next]);
    lv_label_set_text(next_time_label, next_time);
    lv_obj_align(next_time_label, LV_ALIGN_TOP_RIGHT, -12, 27);

    int countdown = next_minutes - current_minute;
    if (countdown <= 0) countdown += 1440;
    const unsigned countdown_value = (unsigned)(countdown % 1441);
    char countdown_text[16];
    snprintf(countdown_text, sizeof(countdown_text), "IN %02u:%02u",
        countdown_value / 60, countdown_value % 60);
    lv_label_set_text(countdown_label, countdown_text);
    lv_obj_align(countdown_label, LV_ALIGN_BOTTOM_RIGHT, -12, -7);

    lvgl_port_unlock();
}
