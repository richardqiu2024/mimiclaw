#include "ui/touch_calibration_screen.h"

#include <stdio.h>

#include "buttons/button_driver.h"
#include "display/display_panel.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui/config_screen.h"

static const char *TAG = "touch_cal";

typedef struct {
    const char *name;
    lv_coord_t x;
    lv_coord_t y;
    lv_coord_t label_x;
    lv_coord_t label_y;
    lv_obj_t *dot;
    lv_obj_t *label;
} touch_target_t;

static bool s_active = false;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_mode_value = NULL;
static lv_obj_t *s_status_primary = NULL;
static lv_obj_t *s_status_secondary = NULL;
static lv_obj_t *s_touch_marker = NULL;

static touch_target_t s_targets[] = {
    { "TOP", 180, 96, 164, 62, NULL, NULL },
    { "RIGHT", 274, 180, 288, 172, NULL, NULL },
    { "BOTTOM", 180, 264, 150, 278, NULL, NULL },
    { "LEFT", 86, 180, 22, 172, NULL, NULL },
    { "CENTER", 180, 180, 150, 194, NULL, NULL },
};

static const display_panel_touch_transform_t s_transform_modes[] = {
    DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_XY,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_X,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_Y,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_XY,
    DISPLAY_PANEL_TOUCH_TRANSFORM_NONE,
    DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_X,
    DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_Y,
};

static const char *transform_mode_text(display_panel_touch_transform_t mode)
{
    switch (mode) {
    case DISPLAY_PANEL_TOUCH_TRANSFORM_NONE:
        return "RAW";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY:
        return "SWAP";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_X:
        return "SWAP+MX";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_Y:
        return "SWAP+MY";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_XY:
        return "SWAP+MXY";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_X:
        return "MX";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_Y:
        return "MY";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_XY:
        return "MXY";
    default:
        return "UNKNOWN";
    }
}

static void set_mode_label_locked(void)
{
    if (s_mode_value == NULL) {
        return;
    }

    lv_label_set_text(
        s_mode_value, transform_mode_text(display_panel_touch_get_transform_mode())
    );
}

static void set_target_highlight_locked(int active_index)
{
    size_t count = sizeof(s_targets) / sizeof(s_targets[0]);
    for (size_t i = 0; i < count; ++i) {
        bool active = ((int)i == active_index);
        lv_obj_set_style_bg_color(
            s_targets[i].dot, active ? lv_color_hex(0x22C55E) : lv_color_hex(0x0F1C2A), 0
        );
        lv_obj_set_style_bg_opa(
            s_targets[i].dot, active ? LV_OPA_COVER : LV_OPA_70, 0
        );
        lv_obj_set_style_border_color(
            s_targets[i].dot, active ? lv_color_hex(0xDCFCE7) : lv_color_hex(0x5DA8D6), 0
        );
        lv_obj_set_style_text_color(
            s_targets[i].label, active ? lv_color_hex(0xF8FAFC) : lv_color_hex(0x8FB7D3), 0
        );
    }
}

static int nearest_target_index(lv_coord_t x, lv_coord_t y)
{
    size_t count = sizeof(s_targets) / sizeof(s_targets[0]);
    int best_index = -1;
    int32_t best_distance = 0;

    for (size_t i = 0; i < count; ++i) {
        int32_t dx = (int32_t)x - (int32_t)s_targets[i].x;
        int32_t dy = (int32_t)y - (int32_t)s_targets[i].y;
        int32_t distance = (dx * dx) + (dy * dy);
        if ((best_index < 0) || (distance < best_distance)) {
            best_index = (int)i;
            best_distance = distance;
        }
    }

    return best_index;
}

static void clear_touch_feedback_locked(void)
{
    lv_obj_add_flag(s_touch_marker, LV_OBJ_FLAG_HIDDEN);
    set_target_highlight_locked(-1);

    if (s_status_primary != NULL) {
        lv_label_set_text(s_status_primary, "POINT --, --");
    }
    if (s_status_secondary != NULL) {
        lv_label_set_text(s_status_secondary, "Tap TOP / RIGHT / BOTTOM / LEFT / CENTER");
    }
}

static void update_touch_feedback_locked(lv_coord_t x, lv_coord_t y)
{
    char line[96];
    int active_target = nearest_target_index(x, y);

    if (s_touch_marker != NULL) {
        lv_obj_clear_flag(s_touch_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_touch_marker, x - 7, y - 7);
    }

    set_target_highlight_locked(active_target);

    if (s_status_primary != NULL) {
        snprintf(line, sizeof(line), "POINT %d, %d", (int)x, (int)y);
        lv_label_set_text(s_status_primary, line);
    }
    if (s_status_secondary != NULL) {
        snprintf(
            line, sizeof(line), "NEAREST %s", (active_target >= 0) ? s_targets[active_target].name : "--"
        );
        lv_label_set_text(s_status_secondary, line);
    }
}

static void cycle_transform_mode_locked(int step)
{
    size_t count = sizeof(s_transform_modes) / sizeof(s_transform_modes[0]);
    display_panel_touch_transform_t current = display_panel_touch_get_transform_mode();
    size_t current_index = 0;

    for (size_t i = 0; i < count; ++i) {
        if (s_transform_modes[i] == current) {
            current_index = i;
            break;
        }
    }

    int next_index = (int)current_index + step;
    if (next_index < 0) {
        next_index = (int)count - 1;
    } else if (next_index >= (int)count) {
        next_index = 0;
    }

    if (display_panel_touch_set_transform_mode(s_transform_modes[next_index]) == ESP_OK) {
        set_mode_label_locked();
        clear_touch_feedback_locked();
    }
}

static void touch_capture_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = NULL;
    lv_point_t point = { 0, 0 };

    if ((code != LV_EVENT_PRESSED) && (code != LV_EVENT_PRESSING)) {
        return;
    }

    indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    lv_indev_get_point(indev, &point);
    update_touch_feedback_locked(point.x, point.y);
}

static void button_timer_cb(lv_timer_t *timer)
{
    PressEvent event = BOOT_KEY_State;

    (void)timer;

    if (!s_active || (event == NONE_PRESS)) {
        return;
    }

    BOOT_KEY_State = NONE_PRESS;

    if (event == SINGLE_CLICK) {
        cycle_transform_mode_locked(1);
        return;
    }

    if (event == DOUBLE_CLICK) {
        cycle_transform_mode_locked(-1);
        return;
    }

    if (event == LONG_PRESS_START) {
        s_active = false;
        (void)config_screen_init();
    }
}

static lv_obj_t *create_target_dot(lv_obj_t *parent, touch_target_t *target)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 18, 18);
    lv_obj_set_pos(dot, target->x - 9, target->y - 9);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x0F1C2A), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_70, 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(0x5DA8D6), 0);
    return dot;
}

static lv_obj_t *create_target_label(lv_obj_t *parent, touch_target_t *target)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, target->name);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x8FB7D3), 0);
    lv_obj_set_pos(label, target->label_x, target->label_y);
    return label;
}

static void build_screen_locked(void)
{
    size_t count = sizeof(s_targets) / sizeof(s_targets[0]);

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x07111F), 0);
    lv_obj_set_style_bg_grad_color(s_screen, lv_color_hex(0x15344B), 0);
    lv_obj_set_style_bg_grad_dir(s_screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *brand_chip = lv_obj_create(s_screen);
    lv_obj_remove_style_all(brand_chip);
    lv_obj_set_size(brand_chip, 112, 30);
    lv_obj_align(brand_chip, LV_ALIGN_TOP_LEFT, 18, 18);
    lv_obj_set_style_radius(brand_chip, 15, 0);
    lv_obj_set_style_bg_color(brand_chip, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_opa(brand_chip, LV_OPA_70, 0);
    lv_obj_set_style_border_width(brand_chip, 1, 0);
    lv_obj_set_style_border_color(brand_chip, lv_color_hex(0x17304A), 0);

    lv_obj_t *brand_label = lv_label_create(brand_chip);
    lv_label_set_text(brand_label, "TOUCH CAL");
    lv_obj_set_style_text_font(brand_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(brand_label, lv_color_hex(0xD7E7F3), 0);
    lv_obj_center(brand_label);

    lv_obj_t *mode_chip = lv_obj_create(s_screen);
    lv_obj_remove_style_all(mode_chip);
    lv_obj_set_size(mode_chip, 104, 30);
    lv_obj_align(mode_chip, LV_ALIGN_TOP_RIGHT, -18, 18);
    lv_obj_set_style_radius(mode_chip, 15, 0);
    lv_obj_set_style_bg_color(mode_chip, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_opa(mode_chip, LV_OPA_70, 0);
    lv_obj_set_style_border_width(mode_chip, 1, 0);
    lv_obj_set_style_border_color(mode_chip, lv_color_hex(0x17304A), 0);

    s_mode_value = lv_label_create(mode_chip);
    lv_label_set_text(s_mode_value, "--");
    lv_obj_set_style_text_font(s_mode_value, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_mode_value, lv_color_hex(0xF8FAFC), 0);
    lv_obj_center(s_mode_value);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "Touch Direction Calibration");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF7FAFC), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 56);

    lv_obj_t *target_guide = lv_label_create(s_screen);
    lv_obj_set_width(target_guide, 296);
    lv_label_set_long_mode(target_guide, LV_LABEL_LONG_WRAP);
    lv_label_set_text(
        target_guide,
        "T 180,96   R 274,180\nB 180,264   L 86,180   C 180,180"
    );
    lv_obj_set_style_text_font(target_guide, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(target_guide, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(target_guide, lv_color_hex(0x90A8BC), 0);
    lv_obj_align(target_guide, LV_ALIGN_TOP_MID, 0, 82);

    lv_obj_t *guide_ring = lv_obj_create(s_screen);
    lv_obj_remove_style_all(guide_ring);
    lv_obj_set_size(guide_ring, 226, 226);
    lv_obj_align(guide_ring, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_radius(guide_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(guide_ring, LV_OPA_0, 0);
    lv_obj_set_style_border_width(guide_ring, 1, 0);
    lv_obj_set_style_border_color(guide_ring, lv_color_hex(0x29475E), 0);

    for (size_t i = 0; i < count; ++i) {
        s_targets[i].dot = create_target_dot(s_screen, &s_targets[i]);
        s_targets[i].label = create_target_label(s_screen, &s_targets[i]);
    }

    s_touch_marker = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_touch_marker);
    lv_obj_set_size(s_touch_marker, 14, 14);
    lv_obj_set_style_radius(s_touch_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_touch_marker, lv_color_hex(0xF97316), 0);
    lv_obj_set_style_bg_opa(s_touch_marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_touch_marker, 2, 0);
    lv_obj_set_style_border_color(s_touch_marker, lv_color_hex(0xFFEDD5), 0);
    lv_obj_add_flag(s_touch_marker, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *status_card = lv_obj_create(s_screen);
    lv_obj_remove_style_all(status_card);
    lv_obj_set_size(status_card, 300, 54);
    lv_obj_align(status_card, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_radius(status_card, 20, 0);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_80, 0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_set_style_border_color(status_card, lv_color_hex(0x17304A), 0);
    lv_obj_set_style_pad_left(status_card, 16, 0);
    lv_obj_set_style_pad_right(status_card, 16, 0);
    lv_obj_set_style_pad_top(status_card, 8, 0);
    lv_obj_set_style_pad_bottom(status_card, 8, 0);

    s_status_primary = lv_label_create(status_card);
    lv_label_set_text(s_status_primary, "POINT --, --");
    lv_obj_set_style_text_font(s_status_primary, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status_primary, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(s_status_primary, LV_ALIGN_TOP_LEFT, 0, 0);

    s_status_secondary = lv_label_create(status_card);
    lv_label_set_text(s_status_secondary, "Tap TOP / RIGHT / BOTTOM / LEFT / CENTER");
    lv_obj_set_width(s_status_secondary, 268);
    lv_label_set_long_mode(s_status_secondary, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_status_secondary, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status_secondary, lv_color_hex(0xC9D8E4), 0);
    lv_obj_align(s_status_secondary, LV_ALIGN_TOP_LEFT, 0, 16);

    lv_obj_t *status_hint = lv_label_create(s_screen);
    lv_label_set_text(status_hint, "BOOT: single next  double prev  long exit");
    lv_obj_set_style_text_font(status_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_hint, lv_color_hex(0x8EA3AF), 0);
    lv_obj_align(status_hint, LV_ALIGN_BOTTOM_MID, 0, -78);

    lv_obj_t *touch_layer = lv_obj_create(s_screen);
    lv_obj_remove_style_all(touch_layer);
    lv_obj_set_size(touch_layer, 360, 360);
    lv_obj_center(touch_layer);
    lv_obj_set_style_bg_opa(touch_layer, LV_OPA_0, 0);
    lv_obj_add_flag(touch_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(
        touch_layer, touch_capture_event_cb, LV_EVENT_PRESSED, NULL
    );
    lv_obj_add_event_cb(
        touch_layer, touch_capture_event_cb, LV_EVENT_PRESSING, NULL
    );

    lv_obj_move_foreground(s_touch_marker);
    set_mode_label_locked();
    clear_touch_feedback_locked();
}

esp_err_t touch_calibration_screen_show(void)
{
    esp_err_t err = ESP_OK;

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
        lv_timer_create(button_timer_cb, 60, NULL);
    }

    set_mode_label_locked();
    clear_touch_feedback_locked();
    lv_scr_load(s_screen);
    s_active = true;

    display_panel_lvgl_unlock();
    ESP_LOGI(TAG, "Touch calibration screen ready");
    return ESP_OK;
}

bool touch_calibration_screen_is_active(void)
{
    return s_active;
}
