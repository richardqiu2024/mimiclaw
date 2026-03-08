#include "ui/config_screen.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "buttons/button_driver.h"
#include "display/display_panel.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs.h"
#include "wifi/wifi_manager.h"

static const char *TAG = "config_screen";

#define CONFIG_SCREEN_SIZE            360
#define CONFIG_SCREEN_CENTER          (CONFIG_SCREEN_SIZE / 2)
#define CONFIG_SCREEN_ROT_NAMESPACE   "display"
#define CONFIG_SCREEN_ROT_KEY         "rotation"
#define CONFIG_SCREEN_ROT_FORMAT_KEY  "rotation_fmt"
#define CONFIG_SCREEN_ROT_FORMAT_V2   2
#define CONFIG_SCREEN_MARKER_INSET    28

static bool s_active = false;
static bool s_details_visible = true;
static bool s_ble_ready = false;
static bool s_agent_ready = false;
static char s_phase_text[32] = "BOOTING";

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_details_panel = NULL;
static lv_obj_t *s_rotation_value = NULL;
static lv_obj_t *s_touch_value = NULL;
static lv_obj_t *s_zone_value = NULL;
static lv_obj_t *s_phase_value = NULL;
static lv_obj_t *s_ble_value = NULL;
static lv_obj_t *s_agent_value = NULL;
static lv_obj_t *s_net_value = NULL;
static lv_obj_t *s_touch_dot = NULL;
static lv_obj_t *s_touch_hline = NULL;
static lv_obj_t *s_touch_vline = NULL;
static lv_timer_t *s_status_timer = NULL;
static lv_timer_t *s_button_timer = NULL;
static lv_point_t s_last_touch_point = {0, 0};
static bool s_has_touch_point = false;

typedef struct {
    const char *name;
    lv_point_t point;
} config_reference_point_t;

static const config_reference_point_t s_reference_points[] = {
    { "TOP",    { CONFIG_SCREEN_CENTER, 0 } },
    { "BOTTOM", { CONFIG_SCREEN_CENTER, CONFIG_SCREEN_SIZE - 1 } },
    { "LEFT",   { 0, CONFIG_SCREEN_CENTER } },
    { "RIGHT",  { CONFIG_SCREEN_SIZE - 1, CONFIG_SCREEN_CENTER } },
    { "CENTER", { CONFIG_SCREEN_CENTER, CONFIG_SCREEN_CENTER } },
};

static lv_coord_t clamp_coord(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value >= CONFIG_SCREEN_SIZE) {
        return CONFIG_SCREEN_SIZE - 1;
    }
    return (lv_coord_t)value;
}

static uint16_t rotation_to_degrees(lv_disp_rot_t rotation)
{
    switch (rotation) {
    case LV_DISP_ROT_90:
        return 90;
    case LV_DISP_ROT_180:
        return 180;
    case LV_DISP_ROT_270:
        return 270;
    case LV_DISP_ROT_NONE:
    default:
        return 0;
    }
}

static lv_disp_rot_t rotation_from_degrees(uint16_t degrees)
{
    switch (degrees) {
    case 90:
        return LV_DISP_ROT_90;
    case 180:
        return LV_DISP_ROT_180;
    case 270:
        return LV_DISP_ROT_270;
    case 0:
    default:
        return LV_DISP_ROT_NONE;
    }
}

static lv_disp_rot_t get_reference_rotation(void)
{
    return rotation_from_degrees(display_panel_get_reference_rotation_degrees());
}

static const char *find_nearest_reference_name(const lv_point_t *point)
{
    uint32_t best_distance = UINT32_MAX;
    const char *best_name = "--";

    for (size_t index = 0; index < sizeof(s_reference_points) / sizeof(s_reference_points[0]); index++) {
        int delta_x = point->x - s_reference_points[index].point.x;
        int delta_y = point->y - s_reference_points[index].point.y;
        uint32_t distance = (uint32_t)(delta_x * delta_x + delta_y * delta_y);
        if (distance < best_distance) {
            best_distance = distance;
            best_name = s_reference_points[index].name;
        }
    }

    return best_name;
}

static esp_err_t load_saved_rotation(lv_disp_rot_t *rotation)
{
    nvs_handle_t handle;
    uint16_t stored_degrees = 0;
    uint8_t rotation_format = 0;
    bool is_legacy_rotation = false;
    esp_err_t err = nvs_open(CONFIG_SCREEN_ROT_NAMESPACE, NVS_READONLY, &handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *rotation = get_reference_rotation();
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u8(handle, CONFIG_SCREEN_ROT_FORMAT_KEY, &rotation_format);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        is_legacy_rotation = true;
    } else if (err == ESP_OK) {
        is_legacy_rotation = (rotation_format != CONFIG_SCREEN_ROT_FORMAT_V2);
    } else if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_get_u16(handle, CONFIG_SCREEN_ROT_KEY, &stored_degrees);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *rotation = get_reference_rotation();
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    if (is_legacy_rotation) {
        stored_degrees = (uint16_t)(
            (stored_degrees + rotation_to_degrees(get_reference_rotation())) % 360
        );
    }

    *rotation = rotation_from_degrees(stored_degrees);
    return ESP_OK;
}

static esp_err_t save_rotation(lv_disp_rot_t rotation)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_SCREEN_ROT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u16(handle, CONFIG_SCREEN_ROT_KEY, rotation_to_degrees(rotation));
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, CONFIG_SCREEN_ROT_FORMAT_KEY, CONFIG_SCREEN_ROT_FORMAT_V2);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

static lv_obj_t *create_overlay_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_70, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x334155), 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    return panel;
}

static lv_obj_t *create_panel_line(lv_obj_t *parent, lv_coord_t y_offset)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_obj_get_width(parent) - 16);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, y_offset);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE2E8F0), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

static lv_obj_t *create_tag_label(
    lv_obj_t *parent, const char *text, lv_coord_t width, lv_color_t text_color, lv_color_t background_color
)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, text_color, 0);
    lv_obj_set_style_bg_color(label, background_color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_70, 0);
    lv_obj_set_style_radius(label, 12, 0);
    lv_obj_set_style_pad_left(label, 10, 0);
    lv_obj_set_style_pad_right(label, 10, 0);
    lv_obj_set_style_pad_top(label, 6, 0);
    lv_obj_set_style_pad_bottom(label, 6, 0);
    return label;
}

static void create_reference_marker(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, uint32_t color_hex)
{
    lv_obj_t *marker = lv_obj_create(parent);
    lv_obj_remove_style_all(marker);
    lv_obj_set_size(marker, 12, 12);
    lv_obj_set_pos(marker, clamp_coord(x - 6), clamp_coord(y - 6));
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(marker, lv_color_hex(color_hex), 0);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(marker, 2, 0);
    lv_obj_set_style_border_color(marker, lv_color_hex(0xF8FAFC), 0);
}

static void clear_touch_feedback_locked(void)
{
    s_has_touch_point = false;

    if (s_touch_value) {
        lv_label_set_text(s_touch_value, "TOUCH --,--");
    }
    if (s_zone_value) {
        lv_label_set_text(s_zone_value, "NEAREST --");
    }
    if (s_touch_dot) {
        lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_touch_hline) {
        lv_obj_add_flag(s_touch_hline, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_touch_vline) {
        lv_obj_add_flag(s_touch_vline, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_rotation_label_locked(void)
{
    lv_disp_t *display = lv_disp_get_default();
    if (!display || !s_rotation_value) {
        return;
    }

    char line[32];
    snprintf(line, sizeof(line), "ROT %u DEG", rotation_to_degrees(lv_disp_get_rotation(display)));
    lv_label_set_text(s_rotation_value, line);
}

static void refresh_static_labels_locked(void)
{
    if (s_phase_value) {
        char line[40];
        snprintf(line, sizeof(line), "PHASE %s", s_phase_text);
        lv_label_set_text(s_phase_value, line);
        lv_obj_set_style_text_color(
            s_phase_value, s_agent_ready ? lv_color_hex(0xA7F3D0) : lv_color_hex(0xFDE68A), 0
        );
    }

    if (s_ble_value) {
        lv_label_set_text(s_ble_value, s_ble_ready ? "BLE READY" : "BLE WAITING");
        lv_obj_set_style_text_color(
            s_ble_value, s_ble_ready ? lv_color_hex(0xA7F3D0) : lv_color_hex(0xFCA5A5), 0
        );
    }

    if (s_agent_value) {
        lv_label_set_text(s_agent_value, s_agent_ready ? "AGENT LIVE" : "AGENT BOOT");
        lv_obj_set_style_text_color(
            s_agent_value, s_agent_ready ? lv_color_hex(0x93C5FD) : lv_color_hex(0xCBD5E1), 0
        );
    }
}

static void refresh_runtime_labels_locked(void)
{
    if (!s_net_value) {
        return;
    }

    bool wifi_connected = wifi_manager_is_connected();
    lv_label_set_text(s_net_value, wifi_connected ? "NET ONLINE" : "NET WAITING");
    lv_obj_set_style_text_color(
        s_net_value, wifi_connected ? lv_color_hex(0x86EFAC) : lv_color_hex(0xCBD5E1), 0
    );
}

static void refresh_touch_feedback_locked(const lv_point_t *point)
{
    lv_coord_t point_x = clamp_coord(point->x);
    lv_coord_t point_y = clamp_coord(point->y);
    char line[48];

    s_last_touch_point.x = point_x;
    s_last_touch_point.y = point_y;
    s_has_touch_point = true;

    if (s_touch_value) {
        snprintf(line, sizeof(line), "TOUCH %03d,%03d", point_x, point_y);
        lv_label_set_text(s_touch_value, line);
    }

    if (s_zone_value) {
        snprintf(line, sizeof(line), "NEAREST %s", find_nearest_reference_name(&s_last_touch_point));
        lv_label_set_text(s_zone_value, line);
    }

    if (s_touch_dot) {
        lv_obj_set_pos(s_touch_dot, clamp_coord(point_x - 9), clamp_coord(point_y - 9));
        lv_obj_clear_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_touch_hline) {
        lv_obj_set_pos(s_touch_hline, 0, point_y);
        lv_obj_clear_flag(s_touch_hline, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_touch_vline) {
        lv_obj_set_pos(s_touch_vline, point_x, 0);
        lv_obj_clear_flag(s_touch_vline, LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_rotation_locked(lv_disp_rot_t rotation, bool persist_rotation)
{
    lv_disp_t *display = lv_disp_get_default();
    if (!display) {
        return;
    }

    lv_disp_set_rotation(display, rotation);
    refresh_rotation_label_locked();
    clear_touch_feedback_locked();

    if (persist_rotation) {
        esp_err_t err = save_rotation(rotation);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Save rotation failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Saved display rotation: %u degrees", rotation_to_degrees(rotation));
        }
    }
}

static void cycle_rotation_locked(void)
{
    lv_disp_t *display = lv_disp_get_default();
    lv_disp_rot_t current_rotation;
    lv_disp_rot_t next_rotation;

    if (!display) {
        return;
    }

    current_rotation = lv_disp_get_rotation(display);
    switch (current_rotation) {
    case LV_DISP_ROT_NONE:
        next_rotation = LV_DISP_ROT_90;
        break;
    case LV_DISP_ROT_90:
        next_rotation = LV_DISP_ROT_180;
        break;
    case LV_DISP_ROT_180:
        next_rotation = LV_DISP_ROT_270;
        break;
    case LV_DISP_ROT_270:
    default:
        next_rotation = LV_DISP_ROT_NONE;
        break;
    }

    apply_rotation_locked(next_rotation, true);
}

static void set_details_visible_locked(bool visible)
{
    s_details_visible = visible;

    if (!s_details_panel) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(s_details_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_details_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void touch_overlay_event_cb(lv_event_t *event)
{
    lv_event_code_t event_code = lv_event_get_code(event);
    lv_indev_t *input_device = lv_indev_get_act();
    lv_point_t point;

    if (!input_device) {
        return;
    }

    if ((event_code != LV_EVENT_PRESSED) &&
            (event_code != LV_EVENT_PRESSING) &&
            (event_code != LV_EVENT_CLICKED) &&
            (event_code != LV_EVENT_RELEASED)) {
        return;
    }

    lv_indev_get_point(input_device, &point);
    refresh_touch_feedback_locked(&point);
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_runtime_labels_locked();
}

static void button_timer_cb(lv_timer_t *timer)
{
    PressEvent button_event;

    (void)timer;

    button_event = BOOT_KEY_State;
    if (button_event == NONE_PRESS) {
        return;
    }

    BOOT_KEY_State = NONE_PRESS;

    switch (button_event) {
    case SINGLE_CLICK:
        cycle_rotation_locked();
        break;
    case DOUBLE_CLICK:
        set_details_visible_locked(!s_details_visible);
        break;
    case LONG_PRESS_START:
        apply_rotation_locked(get_reference_rotation(), true);
        break;
    default:
        break;
    }
}

static void build_screen_locked(void)
{
    lv_obj_t *frame;
    lv_obj_t *center_hline;
    lv_obj_t *center_vline;
    lv_obj_t *touch_overlay;
    lv_obj_t *left_panel;
    lv_obj_t *right_panel;
    lv_obj_t *hint_label;
    lv_obj_t *center_label;

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_grad_color(s_screen, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_grad_dir(s_screen, LV_GRAD_DIR_VER, 0);

    frame = lv_obj_create(s_screen);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, 356, 356);
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(frame, 24, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_0, 0);

    center_hline = lv_obj_create(s_screen);
    lv_obj_remove_style_all(center_hline);
    lv_obj_set_size(center_hline, 320, 1);
    lv_obj_align(center_hline, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(center_hline, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(center_hline, lv_color_hex(0x475569), 0);

    center_vline = lv_obj_create(s_screen);
    lv_obj_remove_style_all(center_vline);
    lv_obj_set_size(center_vline, 1, 320);
    lv_obj_align(center_vline, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(center_vline, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(center_vline, lv_color_hex(0x475569), 0);

    lv_obj_t *top_label = create_tag_label(
        s_screen, "TOP\n180,0", 70, lv_color_hex(0xFED7AA), lv_color_hex(0x7C2D12)
    );
    lv_obj_align(top_label, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *bottom_label = create_tag_label(
        s_screen, "BOTTOM\n180,359", 100, lv_color_hex(0xA7F3D0), lv_color_hex(0x064E3B)
    );
    lv_obj_align(bottom_label, LV_ALIGN_BOTTOM_MID, 0, -48);

    lv_obj_t *left_label = create_tag_label(
        s_screen, "LEFT\n0,180", 74, lv_color_hex(0xBAE6FD), lv_color_hex(0x0C4A6E)
    );
    lv_obj_align(left_label, LV_ALIGN_LEFT_MID, 46, 0);

    lv_obj_t *right_label = create_tag_label(
        s_screen, "RIGHT\n359,180", 84, lv_color_hex(0xDDD6FE), lv_color_hex(0x4C1D95)
    );
    lv_obj_align(right_label, LV_ALIGN_RIGHT_MID, -46, 0);

    center_label = create_tag_label(
        s_screen, "CENTER\n180,180", 104, lv_color_hex(0xF8FAFC), lv_color_hex(0x1E293B)
    );
    lv_obj_align(center_label, LV_ALIGN_CENTER, 0, 34);

    create_reference_marker(s_screen, CONFIG_SCREEN_CENTER, CONFIG_SCREEN_MARKER_INSET, 0xF97316);
    create_reference_marker(
        s_screen, CONFIG_SCREEN_CENTER, CONFIG_SCREEN_SIZE - 1 - CONFIG_SCREEN_MARKER_INSET, 0x10B981
    );
    create_reference_marker(s_screen, CONFIG_SCREEN_MARKER_INSET, CONFIG_SCREEN_CENTER, 0x38BDF8);
    create_reference_marker(
        s_screen, CONFIG_SCREEN_SIZE - 1 - CONFIG_SCREEN_MARKER_INSET, CONFIG_SCREEN_CENTER, 0xA78BFA
    );
    create_reference_marker(s_screen, CONFIG_SCREEN_CENTER, CONFIG_SCREEN_CENTER, 0xF8FAFC);

    s_touch_hline = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_touch_hline);
    lv_obj_set_size(s_touch_hline, CONFIG_SCREEN_SIZE, 2);
    lv_obj_set_style_bg_color(s_touch_hline, lv_color_hex(0xF43F5E), 0);
    lv_obj_set_style_bg_opa(s_touch_hline, LV_OPA_80, 0);
    lv_obj_add_flag(s_touch_hline, LV_OBJ_FLAG_HIDDEN);

    s_touch_vline = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_touch_vline);
    lv_obj_set_size(s_touch_vline, 2, CONFIG_SCREEN_SIZE);
    lv_obj_set_style_bg_color(s_touch_vline, lv_color_hex(0xF43F5E), 0);
    lv_obj_set_style_bg_opa(s_touch_vline, LV_OPA_80, 0);
    lv_obj_add_flag(s_touch_vline, LV_OBJ_FLAG_HIDDEN);

    s_touch_dot = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_touch_dot);
    lv_obj_set_size(s_touch_dot, 18, 18);
    lv_obj_set_style_radius(s_touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_touch_dot, lv_color_hex(0xF43F5E), 0);
    lv_obj_set_style_bg_opa(s_touch_dot, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_touch_dot, 2, 0);
    lv_obj_set_style_border_color(s_touch_dot, lv_color_hex(0xFFE4E6), 0);
    lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);

    s_details_panel = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_details_panel);
    lv_obj_set_size(s_details_panel, CONFIG_SCREEN_SIZE, 118);
    lv_obj_set_pos(s_details_panel, 0, 0);
    lv_obj_set_style_bg_opa(s_details_panel, LV_OPA_0, 0);

    left_panel = create_overlay_panel(s_details_panel, 10, 10, 132, 82);
    s_rotation_value = create_panel_line(left_panel, 0);
    s_touch_value = create_panel_line(left_panel, 18);
    s_zone_value = create_panel_line(left_panel, 36);
    lv_obj_t *size_value = create_panel_line(left_panel, 54);
    lv_label_set_text(size_value, "SIZE 360x360");

    right_panel = create_overlay_panel(s_details_panel, 218, 10, 132, 82);
    s_phase_value = create_panel_line(right_panel, 0);
    s_ble_value = create_panel_line(right_panel, 18);
    s_agent_value = create_panel_line(right_panel, 36);
    s_net_value = create_panel_line(right_panel, 54);

    hint_label = lv_label_create(s_details_panel);
    lv_obj_set_width(hint_label, 320);
    lv_label_set_text(hint_label, "Boot: 1x rotate  2x info  long ref | Touch to read XY");
    lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);

    touch_overlay = lv_obj_create(s_screen);
    lv_obj_remove_style_all(touch_overlay);
    lv_obj_set_size(touch_overlay, CONFIG_SCREEN_SIZE, CONFIG_SCREEN_SIZE);
    lv_obj_set_pos(touch_overlay, 0, 0);
    lv_obj_set_style_bg_opa(touch_overlay, LV_OPA_0, 0);
    lv_obj_add_flag(touch_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(touch_overlay, touch_overlay_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_move_foreground(s_touch_hline);
    lv_obj_move_foreground(s_touch_vline);
    lv_obj_move_foreground(s_touch_dot);
    lv_obj_move_foreground(s_details_panel);
    lv_obj_move_foreground(touch_overlay);

    refresh_rotation_label_locked();
    refresh_static_labels_locked();
    refresh_runtime_labels_locked();
    clear_touch_feedback_locked();
}

esp_err_t config_screen_init(void)
{
    esp_err_t err;
    lv_disp_rot_t saved_rotation = get_reference_rotation();

    if (!display_panel_is_ready()) {
        ESP_LOGW(TAG, "Display is not ready, skip config screen");
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
        s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
        s_button_timer = lv_timer_create(button_timer_cb, 50, NULL);
    }

    lv_scr_load(s_screen);
    set_details_visible_locked(s_details_visible);

    err = load_saved_rotation(&saved_rotation);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Load rotation failed: %s", esp_err_to_name(err));
        saved_rotation = LV_DISP_ROT_NONE;
    }
    apply_rotation_locked(saved_rotation, false);

    refresh_static_labels_locked();
    refresh_runtime_labels_locked();
    s_active = true;

    display_panel_lvgl_unlock();
    ESP_LOGI(TAG, "LVGL orientation screen ready");
    return ESP_OK;
}

void config_screen_set_phase(const char *phase)
{
    const char *next_phase = (phase && phase[0]) ? phase : "BOOTING";
    snprintf(s_phase_text, sizeof(s_phase_text), "%s", next_phase);

    if (display_panel_lvgl_is_ready() && display_panel_lvgl_lock(100)) {
        refresh_static_labels_locked();
        display_panel_lvgl_unlock();
    }
}

void config_screen_set_ble_ready(bool ready)
{
    s_ble_ready = ready;

    if (display_panel_lvgl_is_ready() && display_panel_lvgl_lock(100)) {
        refresh_static_labels_locked();
        display_panel_lvgl_unlock();
    }
}

void config_screen_set_agent_ready(bool ready)
{
    s_agent_ready = ready;

    if (display_panel_lvgl_is_ready() && display_panel_lvgl_lock(100)) {
        refresh_static_labels_locked();
        display_panel_lvgl_unlock();
    }
}

void config_screen_toggle(void)
{
    if (!display_panel_lvgl_is_ready()) {
        return;
    }
    if (!display_panel_lvgl_lock(100)) {
        return;
    }

    set_details_visible_locked(!s_details_visible);

    display_panel_lvgl_unlock();
}

bool config_screen_is_active(void)
{
    return s_active;
}

void config_screen_scroll_down(void)
{
    if (!display_panel_lvgl_is_ready()) {
        return;
    }
    if (!display_panel_lvgl_lock(100)) {
        return;
    }

    cycle_rotation_locked();

    display_panel_lvgl_unlock();
}
