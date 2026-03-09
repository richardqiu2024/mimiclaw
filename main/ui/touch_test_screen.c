#include "ui/touch_test_screen.h"

#include <inttypes.h>
#include <stdio.h>

#include "display/display_panel.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "touch_test";

#define TOUCH_TEST_SCREEN_SIZE    360

static bool s_active = false;
static bool s_last_pressed = false;
static uint16_t s_last_x = 0;
static uint16_t s_last_y = 0;
static uint16_t s_last_strength = 0;
static uint32_t s_press_count = 0;
static uint32_t s_sample_count = 0;
static uint32_t s_error_count = 0;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_point_label = NULL;
static lv_obj_t *s_strength_label = NULL;
static lv_obj_t *s_counter_label = NULL;
static lv_obj_t *s_flags_label = NULL;
static lv_obj_t *s_hint_label = NULL;
static lv_obj_t *s_touch_dot = NULL;
static lv_obj_t *s_touch_hline = NULL;
static lv_obj_t *s_touch_vline = NULL;
static lv_timer_t *s_poll_timer = NULL;

static lv_coord_t clamp_coord(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value >= TOUCH_TEST_SCREEN_SIZE) {
        return TOUCH_TEST_SCREEN_SIZE - 1;
    }
    return (lv_coord_t)value;
}

static void update_touch_overlay(bool pressed, uint16_t x, uint16_t y)
{
    if ((s_touch_dot == NULL) || (s_touch_hline == NULL) || (s_touch_vline == NULL)) {
        return;
    }

    if (!pressed) {
        lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_touch_hline, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_touch_vline, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_set_pos(s_touch_dot, clamp_coord((int)x - 10), clamp_coord((int)y - 10));
    lv_obj_set_pos(s_touch_hline, 0, clamp_coord(y));
    lv_obj_set_pos(s_touch_vline, clamp_coord(x), 0);

    lv_obj_clear_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_touch_hline, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_touch_vline, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_flags_label(void)
{
    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    bool interrupt_enabled = false;

    if (s_flags_label == NULL) {
        return;
    }

    if (display_panel_touch_get_flags(&swap_xy, &mirror_x, &mirror_y, &interrupt_enabled) == ESP_OK) {
        lv_label_set_text_fmt(
            s_flags_label, "DRV READY  SWAP:%d  MX:%d  MY:%d  INT:%d",
            swap_xy ? 1 : 0, mirror_x ? 1 : 0, mirror_y ? 1 : 0, interrupt_enabled ? 1 : 0
        );
    } else {
        lv_label_set_text(s_flags_label, "DRV NOT READY");
    }
}

static void refresh_labels(bool pressed)
{
    if (s_status_label != NULL) {
        lv_label_set_text(s_status_label, pressed ? "STATUS PRESSED" : "STATUS WAITING");
        lv_obj_set_style_text_color(
            s_status_label, pressed ? lv_color_hex(0x86EFAC) : lv_color_hex(0xFDE68A), 0
        );
    }

    if (s_point_label != NULL) {
        lv_label_set_text_fmt(s_point_label, "POINT %03u,%03u", s_last_x, s_last_y);
    }

    if (s_strength_label != NULL) {
        lv_label_set_text_fmt(s_strength_label, "STRENGTH %03u", s_last_strength);
    }

    if (s_counter_label != NULL) {
        lv_label_set_text_fmt(
            s_counter_label, "PRESS %" PRIu32 "  SAMPLE %" PRIu32 "  ERR %" PRIu32,
            s_press_count, s_sample_count, s_error_count
        );
    }
}

static void touch_poll_timer_cb(lv_timer_t *timer)
{
    uint16_t point_x = s_last_x;
    uint16_t point_y = s_last_y;
    uint16_t strength = 0;
    bool pressed = false;
    esp_err_t err;

    (void)timer;

    err = display_panel_touch_read_point(&point_x, &point_y, &strength, &pressed);
    if (err == ESP_ERR_NOT_FOUND) {
        refresh_flags_label();
        if (s_hint_label != NULL) {
            lv_label_set_text(s_hint_label, "Touch driver missing. Check CST816S bus/pins.");
        }
        update_touch_overlay(false, 0, 0);
        refresh_labels(false);
        return;
    }
    if (err != ESP_OK) {
        s_error_count++;
        if (s_hint_label != NULL) {
            lv_label_set_text_fmt(s_hint_label, "Touch read error: %s", esp_err_to_name(err));
        }
        ESP_LOGW(TAG, "Touch read failed: %s", esp_err_to_name(err));
        update_touch_overlay(false, 0, 0);
        refresh_labels(false);
        return;
    }

    if (pressed) {
        if (!s_last_pressed) {
            s_press_count++;
            ESP_LOGI(TAG, "Touch press #%lu at %u,%u strength=%u", (unsigned long)s_press_count, point_x, point_y, strength);
        }
        s_sample_count++;
        s_last_x = point_x;
        s_last_y = point_y;
        s_last_strength = strength;
        if (s_hint_label != NULL) {
            lv_label_set_text(s_hint_label, "Tap / drag on the screen. Raw driver polling is active.");
        }
    } else if (s_last_pressed) {
        ESP_LOGI(TAG, "Touch release at %u,%u", s_last_x, s_last_y);
    }

    s_last_pressed = pressed;
    refresh_flags_label();
    refresh_labels(pressed);
    update_touch_overlay(pressed, s_last_x, s_last_y);
}

static lv_obj_t *create_info_label(lv_obj_t *parent, lv_coord_t y, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, 320);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE2E8F0), 0);
    return label;
}

static void build_screen_locked(void)
{
    lv_obj_t *frame = NULL;
    lv_obj_t *title = NULL;
    lv_obj_t *subtitle = NULL;

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x020617), 0);

    frame = lv_obj_create(s_screen);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, 356, 356);
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(frame, 178, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x1D4ED8), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_0, 0);

    title = lv_label_create(s_screen);
    lv_label_set_text(title, "TOUCH TEST");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), 0);

    subtitle = lv_label_create(s_screen);
    lv_label_set_text(subtitle, "CST816S raw polling");
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x93C5FD), 0);

    s_status_label = create_info_label(s_screen, 100, &lv_font_montserrat_18);
    s_point_label = create_info_label(s_screen, 136, &lv_font_montserrat_16);
    s_strength_label = create_info_label(s_screen, 164, &lv_font_montserrat_16);
    s_counter_label = create_info_label(s_screen, 194, &lv_font_montserrat_14);
    s_flags_label = create_info_label(s_screen, 224, &lv_font_montserrat_12);

    s_hint_label = lv_label_create(s_screen);
    lv_obj_set_width(s_hint_label, 300);
    lv_obj_align(s_hint_label, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_text_align(s_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_hint_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0xCBD5E1), 0);
    lv_label_set_text(s_hint_label, "Bring finger to the panel. Wait for serial logs and dot movement.");

    s_touch_hline = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_touch_hline);
    lv_obj_set_size(s_touch_hline, TOUCH_TEST_SCREEN_SIZE, 2);
    lv_obj_set_style_bg_color(s_touch_hline, lv_color_hex(0xF43F5E), 0);
    lv_obj_set_style_bg_opa(s_touch_hline, LV_OPA_80, 0);
    lv_obj_add_flag(s_touch_hline, LV_OBJ_FLAG_HIDDEN);

    s_touch_vline = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_touch_vline);
    lv_obj_set_size(s_touch_vline, 2, TOUCH_TEST_SCREEN_SIZE);
    lv_obj_set_style_bg_color(s_touch_vline, lv_color_hex(0xF43F5E), 0);
    lv_obj_set_style_bg_opa(s_touch_vline, LV_OPA_80, 0);
    lv_obj_add_flag(s_touch_vline, LV_OBJ_FLAG_HIDDEN);

    s_touch_dot = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_touch_dot);
    lv_obj_set_size(s_touch_dot, 20, 20);
    lv_obj_set_style_radius(s_touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_touch_dot, lv_color_hex(0xF43F5E), 0);
    lv_obj_set_style_bg_opa(s_touch_dot, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_touch_dot, 2, 0);
    lv_obj_set_style_border_color(s_touch_dot, lv_color_hex(0xFFE4E6), 0);
    lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);

    lv_obj_move_foreground(s_touch_hline);
    lv_obj_move_foreground(s_touch_vline);
    lv_obj_move_foreground(s_touch_dot);

    refresh_flags_label();
    refresh_labels(false);
}

esp_err_t touch_test_screen_init(void)
{
    esp_err_t err;

    if (!display_panel_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    err = display_panel_init_lvgl();
    if (err != ESP_OK) {
        return err;
    }

    if (!display_panel_lvgl_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_screen == NULL) {
        build_screen_locked();
        s_poll_timer = lv_timer_create(touch_poll_timer_cb, 30, NULL);
    }

    lv_scr_load(s_screen);
    display_panel_lvgl_unlock();

    s_active = true;
    ESP_LOGI(TAG, "Touch test screen ready");
    if (display_panel_touch_is_ready()) {
        ESP_LOGI(TAG, "Touch driver detected");
    } else {
        ESP_LOGW(TAG, "Touch driver not detected");
    }

    return ESP_OK;
}

bool touch_test_screen_is_active(void)
{
    return s_active;
}
