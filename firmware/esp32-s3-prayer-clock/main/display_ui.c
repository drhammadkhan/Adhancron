#include "display_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "board_config.h"
#include "eid.h"
#include "prayer_times.h"
#include "ramadan.h"

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
static const uint32_t COLOR_FOCUS_BACKGROUND = 0x0B171B;
static const uint32_t COLOR_FOCUS_PANEL = 0x163B38;
static const uint32_t COLOR_FOCUS_INK = 0xF6F1E5;
static const uint32_t COLOR_FOCUS_MUTED = 0x82A5A4;
static const uint32_t COLOR_FOCUS_LINE = 0x294946;

static lv_display_t *lvgl_display;
static lv_obj_t *dashboard;
static lv_obj_t *focus_dashboard;
static lv_obj_t *message_panel;
static lv_obj_t *message_title;
static lv_obj_t *message_detail;
static lv_obj_t *location_label;
static lv_obj_t *address_label;
static lv_obj_t *battery_label;
static lv_obj_t *clock_digit_labels[6];
static lv_obj_t *date_label;
static lv_obj_t *wifi_icon;
static lv_obj_t *audio_icon;
static lv_obj_t *next_eyebrow_label;
static lv_obj_t *next_name_label;
static lv_obj_t *next_time_label;
static lv_obj_t *countdown_label;
static lv_obj_t *prayer_rows[6];
static lv_obj_t *prayer_name_labels[6];
static lv_obj_t *prayer_time_labels[6];
static lv_obj_t *focus_location_label;
static lv_obj_t *focus_address_label;
static lv_obj_t *focus_battery_label;
static lv_obj_t *focus_clock_digit_labels[6];
static lv_obj_t *focus_date_label;
static lv_obj_t *focus_wifi_icon;
static lv_obj_t *focus_audio_icon;
static lv_obj_t *focus_eyebrow_label;
static lv_obj_t *focus_name_label;
static lv_obj_t *focus_time_label;
static lv_obj_t *focus_countdown_label;
static lv_obj_t *focus_prayer_cells[5];
static lv_obj_t *focus_prayer_name_labels[5];
static lv_obj_t *focus_prayer_time_labels[5];

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
    lv_obj_set_pos(panel, 10, 106);
    lv_obj_set_size(panel, 220, 70);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_bg_color(panel, color(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, color(COLOR_DIVIDER), 0);

    next_eyebrow_label = make_label(
        panel, "NEXT PRAYER", &lv_font_montserrat_12, COLOR_GOLD);
    lv_obj_set_pos(next_eyebrow_label, 12, 8);

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
        lv_obj_set_pos(prayer_rows[row], 10, 178 + row * 21);
        lv_obj_set_size(prayer_rows[row], 220, 21);
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
    lv_obj_t *product_label = make_label(
        dashboard, "ADHAN CLOCK", &lv_font_montserrat_12, COLOR_GOLD);
    lv_obj_set_pos(product_label, 52, 4);
    lv_obj_set_size(product_label, 136, 14);
    lv_obj_set_style_text_align(product_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(product_label, LV_LABEL_LONG_MODE_DOTS);

    location_label = make_label(dashboard, "LOCATION", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_set_pos(location_label, 52, 20);
    lv_obj_set_size(location_label, 136, 18);
    lv_obj_set_style_text_align(location_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(location_label, LV_LABEL_LONG_MODE_DOTS);

    wifi_icon = make_label(dashboard, LV_SYMBOL_WIFI, &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_pos(wifi_icon, 188, 13);
    audio_icon = make_label(dashboard, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_pos(audio_icon, 215, 13);

    lv_obj_t *clock_row = lv_obj_create(dashboard);
    make_plain(clock_row);
    lv_obj_set_size(clock_row, 220, 44);
    lv_obj_set_pos(clock_row, 10, 39);
    int clock_x = 26;
    for (int digit = 0; digit < 6; digit++) {
        if (digit == 2 || digit == 4) {
            lv_obj_t *separator = make_label(
                clock_row, ":", &lv_font_montserrat_36, COLOR_IVORY);
            lv_obj_set_size(separator, 12, 44);
            lv_obj_set_pos(separator, clock_x, 0);
            lv_obj_set_style_text_align(separator, LV_TEXT_ALIGN_CENTER, 0);
            clock_x += 12;
        }
        clock_digit_labels[digit] = make_label(
            clock_row, "-", &lv_font_montserrat_36, COLOR_IVORY);
        lv_obj_set_size(clock_digit_labels[digit], 24, 44);
        lv_obj_set_pos(clock_digit_labels[digit], clock_x, 0);
        lv_obj_set_style_text_align(
            clock_digit_labels[digit], LV_TEXT_ALIGN_CENTER, 0);
        clock_x += 24;
    }

    date_label = make_label(dashboard, "WAITING FOR TIME", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 88);

    create_next_prayer_panel(dashboard);
    create_prayer_table(dashboard);

    address_label = make_label(dashboard, "IP: OFFLINE", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_set_pos(address_label, 30, 305);
    lv_obj_set_size(address_label, 180, 14);
    lv_obj_set_style_text_align(address_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(address_label, LV_LABEL_LONG_MODE_DOTS);

    battery_label = make_label(dashboard, "", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_set_pos(battery_label, 180, 305);
    lv_obj_set_size(battery_label, 56, 14);
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
}

static void create_focus_dashboard(lv_obj_t *screen) {
    static const char *names[] = {"FAJR", "DHUHR", "ASR", "MAGHRIB", "ISHA"};

    focus_dashboard = lv_obj_create(screen);
    make_plain(focus_dashboard);
    lv_obj_set_size(focus_dashboard, BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);
    lv_obj_set_style_bg_color(
        focus_dashboard, color(COLOR_FOCUS_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(focus_dashboard, LV_OPA_COVER, 0);

    lv_obj_t *accent = lv_obj_create(focus_dashboard);
    make_plain(accent);
    lv_obj_set_pos(accent, 86, 7);
    lv_obj_set_size(accent, 68, 2);
    lv_obj_set_style_bg_color(accent, color(COLOR_GOLD), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);

    lv_obj_t *product = make_label(
        focus_dashboard, "ADHAN CLOCK", &lv_font_montserrat_12,
        COLOR_GOLD);
    lv_obj_set_pos(product, 48, 13);
    lv_obj_set_size(product, 144, 16);
    lv_obj_set_style_text_align(product, LV_TEXT_ALIGN_CENTER, 0);

    focus_location_label = make_label(
        focus_dashboard, "LOCATION", &lv_font_montserrat_12,
        COLOR_FOCUS_MUTED);
    lv_obj_set_pos(focus_location_label, 43, 30);
    lv_obj_set_size(focus_location_label, 154, 17);
    lv_obj_set_style_text_align(focus_location_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(focus_location_label, LV_LABEL_LONG_MODE_DOTS);

    focus_wifi_icon = make_label(
        focus_dashboard, LV_SYMBOL_WIFI, &lv_font_montserrat_14,
        COLOR_FOCUS_MUTED);
    lv_obj_set_pos(focus_wifi_icon, 10, 20);
    focus_audio_icon = make_label(
        focus_dashboard, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_14,
        COLOR_FOCUS_MUTED);
    lv_obj_set_pos(focus_audio_icon, 214, 20);

    lv_obj_t *clock_row = lv_obj_create(focus_dashboard);
    make_plain(clock_row);
    lv_obj_set_pos(clock_row, 10, 54);
    lv_obj_set_size(clock_row, 220, 44);
    int clock_x = 26;
    for (int digit = 0; digit < 6; digit++) {
        if (digit == 2 || digit == 4) {
            lv_obj_t *separator = make_label(
                clock_row, ":", &lv_font_montserrat_36, COLOR_FOCUS_INK);
            lv_obj_set_pos(separator, clock_x, 0);
            lv_obj_set_size(separator, 12, 44);
            lv_obj_set_style_text_align(separator, LV_TEXT_ALIGN_CENTER, 0);
            clock_x += 12;
        }
        focus_clock_digit_labels[digit] = make_label(
            clock_row, "-", &lv_font_montserrat_36, COLOR_FOCUS_INK);
        lv_obj_set_pos(focus_clock_digit_labels[digit], clock_x, 0);
        lv_obj_set_size(focus_clock_digit_labels[digit], 24, 44);
        lv_obj_set_style_text_align(
            focus_clock_digit_labels[digit], LV_TEXT_ALIGN_CENTER, 0);
        clock_x += 24;
    }

    focus_date_label = make_label(
        focus_dashboard, "WAITING FOR TIME", &lv_font_montserrat_12,
        COLOR_FOCUS_MUTED);
    lv_obj_set_pos(focus_date_label, 12, 101);
    lv_obj_set_size(focus_date_label, 216, 16);
    lv_obj_set_style_text_align(focus_date_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *panel = lv_obj_create(focus_dashboard);
    make_plain(panel);
    lv_obj_set_pos(panel, 12, 123);
    lv_obj_set_size(panel, 216, 101);
    lv_obj_set_style_radius(panel, 7, 0);
    lv_obj_set_style_bg_color(panel, color(COLOR_FOCUS_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);

    focus_eyebrow_label = make_label(
        panel, "NEXT PRAYER", &lv_font_montserrat_12, COLOR_GOLD);
    lv_obj_set_pos(focus_eyebrow_label, 13, 10);
    focus_name_label = make_label(
        panel, "FAJR", &lv_font_montserrat_20, COLOR_IVORY);
    lv_obj_set_pos(focus_name_label, 13, 34);
    focus_time_label = make_label(
        panel, "--:--", &lv_font_montserrat_36, COLOR_IVORY);
    lv_obj_align(focus_time_label, LV_ALIGN_TOP_RIGHT, -13, 29);
    focus_countdown_label = make_label(
        panel, "IN --:--", &lv_font_montserrat_14, COLOR_SKY);
    lv_obj_set_pos(focus_countdown_label, 13, 75);

    for (int index = 0; index < 5; index++) {
        focus_prayer_cells[index] = lv_obj_create(focus_dashboard);
        make_plain(focus_prayer_cells[index]);
        const int column = index % 2;
        const int row = index / 2;
        const int width = index == 4 ? 220 : 108;
        lv_obj_set_pos(
            focus_prayer_cells[index], 10 + column * 112, 231 + row * 22);
        lv_obj_set_size(focus_prayer_cells[index], width, 22);
        lv_obj_set_style_border_side(
            focus_prayer_cells[index], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(
            focus_prayer_cells[index], index == 4 ? 0 : 1, 0);
        lv_obj_set_style_border_color(
            focus_prayer_cells[index], color(COLOR_FOCUS_LINE), 0);
        focus_prayer_name_labels[index] = make_label(
            focus_prayer_cells[index], names[index], &lv_font_montserrat_12,
            COLOR_FOCUS_MUTED);
        lv_obj_set_width(focus_prayer_name_labels[index], 62);
        lv_obj_set_style_text_align(
            focus_prayer_name_labels[index], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(focus_prayer_name_labels[index], 5, 3);
        focus_prayer_time_labels[index] = make_label(
            focus_prayer_cells[index], "--:--", &lv_font_montserrat_12,
            COLOR_FOCUS_INK);
        lv_obj_set_width(focus_prayer_time_labels[index], 46);
        lv_obj_set_style_text_align(
            focus_prayer_time_labels[index], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(focus_prayer_time_labels[index], LV_ALIGN_TOP_RIGHT, -5, 3);
    }

    focus_address_label = make_label(
        focus_dashboard, "IP: OFFLINE", &lv_font_montserrat_12,
        COLOR_FOCUS_MUTED);
    lv_obj_set_pos(focus_address_label, 8, 303);
    lv_obj_set_size(focus_address_label, 164, 15);
    lv_obj_set_style_text_align(focus_address_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(focus_address_label, LV_LABEL_LONG_MODE_DOTS);

    focus_battery_label = make_label(
        focus_dashboard, "", &lv_font_montserrat_12, COLOR_FOCUS_MUTED);
    lv_obj_set_pos(focus_battery_label, 172, 303);
    lv_obj_set_size(focus_battery_label, 60, 15);
    lv_obj_set_style_text_align(focus_battery_label, LV_TEXT_ALIGN_RIGHT, 0);
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
    create_focus_dashboard(screen);
    create_message_panel(screen);
    lv_obj_add_flag(dashboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(focus_dashboard, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_add_flag(focus_dashboard, LV_OBJ_FLAG_HIDDEN);
}

static void format_clock_minutes(int minutes, char output[6]) {
    const unsigned value = (unsigned)(((minutes % 1440) + 1440) % 1440);
    snprintf(output, 6, "%02u:%02u", value / 60, value % 60);
}

static void compact_location_name(const char *saved_name, char output[32]) {
    if (saved_name == NULL || saved_name[0] == '\0') {
        strlcpy(output, "ADHANCRON", 32);
        return;
    }

    const char *first_comma = strchr(saved_name, ',');
    if (first_comma == NULL) {
        strlcpy(output, saved_name, 32);
        return;
    }

    const size_t first_length = (size_t)(first_comma - saved_name);
    bool has_letter = false;
    bool has_digit = false;
    bool postcode_shape = first_length > 0 && first_length <= 8;
    for (size_t index = 0; index < first_length; index++) {
        const unsigned char character = (unsigned char)saved_name[index];
        has_letter |= isalpha(character) != 0;
        has_digit |= isdigit(character) != 0;
        postcode_shape &= isalnum(character) != 0 || character == ' ';
    }

    if (postcode_shape && has_letter && has_digit) {
        char outward_code[9] = {0};
        const char *outward_end = memchr(saved_name, ' ', first_length);
        const size_t outward_length = outward_end
            ? (size_t)(outward_end - saved_name)
            : first_length;
        memcpy(outward_code, saved_name,
            outward_length < sizeof(outward_code) - 1
                ? outward_length
                : sizeof(outward_code) - 1);

        const char *district_start = first_comma + 1;
        while (*district_start == ' ') district_start++;
        const char *district_end = strchr(district_start, ',');
        const size_t district_length = district_end
            ? (size_t)(district_end - district_start)
            : strlen(district_start);
        char district[24] = {0};
        memcpy(district, district_start,
            district_length < sizeof(district) - 1
                ? district_length
                : sizeof(district) - 1);
        char *upon = strstr(district, " upon ");
        if (upon != NULL) *upon = '\0';
        snprintf(output, 32, "%.8s %.22s", outward_code, district);
        return;
    }

    if (has_letter) {
        const size_t city_length = first_length < 31 ? first_length : 31;
        memcpy(output, saved_name, city_length);
        output[city_length] = '\0';
        return;
    }
    strlcpy(output, saved_name, 32);
}

void display_ui_update(
    const adhan_settings_t *current_settings,
    bool connected,
    bool setup_access_point_active,
    bool storage_mounted,
    bool audio_available,
    bool takbeer_available,
    const char *device_address,
    const battery_status_t *battery) {
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
    const bool focus_style =
        current_settings->display_style == ADHAN_DISPLAY_FOCUS;
    if (focus_style) {
        lv_obj_add_flag(dashboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(focus_dashboard, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(dashboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(focus_dashboard, LV_OBJ_FLAG_HIDDEN);
    }

    char saved_location[64];
    const char *location = current_settings->location_name;
    if (location[0] == '\0') {
        snprintf(saved_location, sizeof(saved_location), "%.4f, %.4f",
            current_settings->latitude, current_settings->longitude);
        location = saved_location;
    }
    char compact_location[32];
    compact_location_name(location, compact_location);
    lv_label_set_text(location_label, compact_location);
    lv_label_set_text(focus_location_label, compact_location);

    char address_text[24];
    snprintf(address_text, sizeof(address_text), "IP: %s",
        device_address != NULL && device_address[0] != '\0' ? device_address : "OFFLINE");
    lv_label_set_text(address_label, address_text);
    lv_label_set_text(focus_address_label, address_text);

    if (battery != NULL && battery->available) {
        const char *symbol = LV_SYMBOL_BATTERY_EMPTY;
        if (battery->percentage >= 90) symbol = LV_SYMBOL_BATTERY_FULL;
        else if (battery->percentage >= 65) symbol = LV_SYMBOL_BATTERY_3;
        else if (battery->percentage >= 35) symbol = LV_SYMBOL_BATTERY_2;
        else if (battery->percentage >= 15) symbol = LV_SYMBOL_BATTERY_1;
        if (battery->charging) symbol = LV_SYMBOL_CHARGE;

        char battery_text[20];
        snprintf(battery_text, sizeof(battery_text), "%s %d%%",
            symbol, battery->percentage);
        lv_label_set_text(battery_label, battery_text);
        lv_label_set_text(focus_battery_label, battery_text);
        lv_obj_set_style_text_color(battery_label, color(
            battery->charging ? COLOR_GOLD :
            (battery->percentage <= 15 ? COLOR_ERROR : COLOR_SKY)), 0);
        lv_obj_set_style_text_color(focus_battery_label, color(
            battery->charging ? COLOR_GOLD :
            (battery->percentage <= 15 ? COLOR_ERROR : COLOR_FOCUS_MUTED)), 0);
        lv_obj_clear_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(focus_battery_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(focus_battery_label, LV_OBJ_FLAG_HIDDEN);
    }

    char clock_text[7];
    snprintf(clock_text, sizeof(clock_text), "%02d%02d%02d",
        local_now.tm_hour, local_now.tm_min, local_now.tm_sec);
    for (int digit = 0; digit < 6; digit++) {
        char value[2] = {clock_text[digit], '\0'};
        lv_label_set_text(clock_digit_labels[digit], value);
    }
    char focus_clock[7];
    snprintf(focus_clock, sizeof(focus_clock), "%02d%02d%02d",
        local_now.tm_hour, local_now.tm_min, local_now.tm_sec);
    for (int digit = 0; digit < 6; digit++) {
        char value[2] = {focus_clock[digit], '\0'};
        lv_label_set_text(focus_clock_digit_labels[digit], value);
    }

    ramadan_status_t ramadan = {0};
    ramadan_status_for_date(
        current_settings->ramadan_start_date,
        current_settings->ramadan_end_date,
        &local_now,
        &ramadan);
    eid_status_t eid = {0};
    eid_status_for_date(
        current_settings->eid_fitr_date,
        current_settings->eid_adha_date,
        &local_now,
        &eid);

    char date_text[32];
    if (eid.active) {
        snprintf(date_text, sizeof(date_text), "EID MUBARAK  |  %s",
            eid.kind == EID_AL_FITR ? "FITR" : "ADHA");
    } else if (ramadan.active) {
        char short_date[12];
        strftime(short_date, sizeof(short_date), "%d %b", &local_now);
        snprintf(date_text, sizeof(date_text), "RAMADAN %d  |  %s",
            ramadan.day_number, short_date);
    } else {
        strftime(date_text, sizeof(date_text), "%A  %d %B", &local_now);
    }
    lv_label_set_text(date_label, date_text);
    lv_label_set_text(focus_date_label, date_text);
    lv_obj_set_style_text_color(
        date_label, color(eid.active ? COLOR_GOLD : COLOR_MUTED), 0);
    lv_obj_set_style_text_color(
        focus_date_label,
        color(eid.active ? COLOR_GOLD : COLOR_FOCUS_MUTED), 0);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 88);

    const uint32_t wifi_color = connected
        ? COLOR_SKY
        : (setup_access_point_active ? COLOR_GOLD : COLOR_MUTED);
    lv_obj_set_style_text_color(wifi_icon, color(wifi_color), 0);
    lv_obj_set_style_text_color(
        focus_wifi_icon,
        color(connected ? COLOR_FOCUS_INK :
            (setup_access_point_active ? COLOR_GOLD : COLOR_FOCUS_MUTED)), 0);
    lv_obj_set_style_text_color(audio_icon, color(audio_available ? COLOR_GOLD : COLOR_ERROR), 0);
    lv_obj_set_style_text_color(
        focus_audio_icon,
        color(audio_available ? COLOR_GOLD : COLOR_ERROR), 0);
    lv_label_set_text(audio_icon, storage_mounted ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_WARNING);
    lv_label_set_text(
        focus_audio_icon,
        storage_mounted ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_WARNING);

    static const int row_indexes[] = {0, 1, 2, 3, 4, 5};
    static const int prayer_indexes[] = {0, 2, 3, 4, 5};
    static const char *prayer_names[] = {"FAJR", "DHUHR", "ASR", "MAGHRIB", "ISHA"};
    static const char *row_names[] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
    for (int row = 0; row < 6; row++) {
        char value[6];
        format_clock_minutes(prayer_time_minutes(&times, row_indexes[row]), value);
        lv_label_set_text(prayer_name_labels[row], row_names[row]);
        lv_label_set_text(prayer_time_labels[row], value);
        lv_obj_set_style_bg_opa(prayer_rows[row], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(
            prayer_name_labels[row], color(row == 1 ? COLOR_SKY : COLOR_MUTED), 0);
    }
    for (int index = 0; index < 5; index++) {
        char value[6];
        format_clock_minutes(
            prayer_time_minutes(&times, prayer_indexes[index]), value);
        lv_label_set_text(focus_prayer_time_labels[index], value);
        lv_obj_set_style_text_color(
            focus_prayer_name_labels[index], color(COLOR_FOCUS_MUTED), 0);
        lv_obj_set_style_text_color(
            focus_prayer_time_labels[index], color(COLOR_FOCUS_INK), 0);
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
    lv_obj_set_style_text_color(
        focus_prayer_name_labels[next], color(COLOR_GOLD), 0);
    lv_obj_set_style_text_color(
        focus_prayer_time_labels[next], color(COLOR_FOCUS_INK), 0);

    const int next_minutes = prayer_time_minutes(&times, prayer_indexes[next]);
    char next_time[6];
    format_clock_minutes(next_minutes, next_time);
    int countdown = next_minutes - current_minute;
    if (countdown <= 0) countdown += 1440;
    const int current_seconds = current_minute * 60 + local_now.tm_sec;
    int countdown_seconds = next_minutes * 60 - current_seconds;
    if (countdown_seconds <= 0) countdown_seconds += 24 * 60 * 60;
    const bool ramadan_event_countdown = ramadan.active &&
        countdown_seconds <= 10 * 60 && (next == 0 || next == 3);
    int next_takbeer = eid_next_takbeer_minute(
        &eid, current_minute,
        current_settings->eid_takbeer_start_minute,
        current_settings->eid_takbeer_end_minute,
        current_settings->eid_takbeer_interval_minutes);
    if (next_takbeer == current_minute && local_now.tm_sec > 0) {
        next_takbeer = eid_next_takbeer_minute(
            &eid, current_minute + 1,
            current_settings->eid_takbeer_start_minute,
            current_settings->eid_takbeer_end_minute,
            current_settings->eid_takbeer_interval_minutes);
    }
    const bool eid_takbeer_panel = takbeer_available && eid.active &&
        next_takbeer >= 0 &&
        current_minute >= current_settings->eid_takbeer_start_minute - 10;

    if (eid_takbeer_panel) {
        char takbeer_time[6];
        format_clock_minutes(next_takbeer, takbeer_time);
        const int takbeer_seconds =
            next_takbeer * 60 - current_seconds;
        char seconds_text[24];
        snprintf(seconds_text, sizeof(seconds_text), "IN %02d:%02d",
            takbeer_seconds / 60, takbeer_seconds % 60);
        lv_label_set_text(next_eyebrow_label, "EID TAKBEER");
        lv_label_set_text(next_name_label,
            eid.kind == EID_AL_FITR ? "AL-FITR" : "AL-ADHA");
        lv_label_set_text(next_time_label, takbeer_time);
        lv_label_set_text(countdown_label, seconds_text);
        lv_label_set_text(focus_eyebrow_label, "EID TAKBEER");
        lv_label_set_text(focus_name_label,
            eid.kind == EID_AL_FITR ? "AL-FITR" : "AL-ADHA");
        lv_label_set_text(focus_time_label, takbeer_time);
        lv_label_set_text(focus_countdown_label, seconds_text);
    } else if (ramadan_event_countdown) {
        const char *event = next == 0 ? "SEHRI ENDS IN" : "IFTAR IN";
        char seconds_text[16];
        snprintf(seconds_text, sizeof(seconds_text), "%02d:%02d",
            countdown_seconds / 60, countdown_seconds % 60);
        lv_label_set_text(next_eyebrow_label, event);
        lv_label_set_text(next_name_label, seconds_text);
        lv_label_set_text(next_time_label, next_time);
        lv_label_set_text(countdown_label, prayer_names[next]);
        lv_label_set_text(focus_eyebrow_label, event);
        lv_label_set_text(focus_name_label, seconds_text);
        lv_label_set_text(focus_time_label, next_time);
        lv_label_set_text(focus_countdown_label, prayer_names[next]);
    } else {
        const unsigned countdown_value = (unsigned)(countdown % 1441);
        char countdown_text[16];
        snprintf(countdown_text, sizeof(countdown_text), "IN %02u:%02u",
            countdown_value / 60, countdown_value % 60);
        lv_label_set_text(next_eyebrow_label, "NEXT PRAYER");
        lv_label_set_text(next_name_label, prayer_names[next]);
        lv_label_set_text(next_time_label, next_time);
        lv_label_set_text(countdown_label, countdown_text);
        lv_label_set_text(focus_eyebrow_label, "NEXT PRAYER");
        lv_label_set_text(focus_name_label, prayer_names[next]);
        lv_label_set_text(focus_time_label, next_time);
        lv_label_set_text(focus_countdown_label, countdown_text);
    }
    lv_obj_align(next_time_label, LV_ALIGN_TOP_RIGHT, -12, 27);
    lv_obj_align(countdown_label, LV_ALIGN_BOTTOM_RIGHT, -12, -7);

    lvgl_port_unlock();
}
