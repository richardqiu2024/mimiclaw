#include "ui/config_screen.h"

#include <stdio.h>
#include <string.h>

#include "display/display_panel.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "wifi/wifi_manager.h"

static const char *TAG = "config_screen";

static bool s_active = false;
static bool s_details_visible = true;
static bool s_ble_ready = false;
static bool s_agent_ready = false;
static char s_phase_text[32] = "BOOTING";

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_details_panel = NULL;
static lv_obj_t *s_mode_value = NULL;
static lv_obj_t *s_phase_value = NULL;
static lv_obj_t *s_ble_value = NULL;
static lv_obj_t *s_wifi_value = NULL;
static lv_obj_t *s_ip_value = NULL;
static lv_obj_t *s_mem_value = NULL;
static lv_timer_t *s_status_timer = NULL;

static lv_obj_t *create_status_card(
    lv_obj_t *parent, lv_coord_t y, lv_color_t accent, const char *title, lv_obj_t **value_label
)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 308, 58);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFDF7), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xDCCFB8), 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0xAA9779), 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    lv_obj_t *dot = lv_obj_create(card);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 18, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, accent, 0);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x7D7466), 0);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 40, -10);

    lv_obj_t *value = lv_label_create(card);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_font(value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0x2C2A26), 0);
    lv_obj_align(value, LV_ALIGN_LEFT_MID, 40, 12);

    if (value_label) {
        *value_label = value;
    }

    return card;
}

static void refresh_static_labels_locked(void)
{
    if (s_mode_value) {
        lv_label_set_text(s_mode_value, s_agent_ready ? "LIVE" : "BOOT");
        lv_obj_set_style_text_color(
            s_mode_value, s_agent_ready ? lv_color_hex(0x0F7B6C) : lv_color_hex(0x915F18), 0
        );
    }

    if (s_phase_value) {
        lv_label_set_text(s_phase_value, s_phase_text);
        lv_obj_set_style_text_color(
            s_phase_value, s_agent_ready ? lv_color_hex(0x0B5E4F) : lv_color_hex(0x2C2A26), 0
        );
    }

    if (s_ble_value) {
        lv_label_set_text(s_ble_value, s_ble_ready ? "READY" : "WAITING");
        lv_obj_set_style_text_color(
            s_ble_value, s_ble_ready ? lv_color_hex(0x0F7B6C) : lv_color_hex(0x915F18), 0
        );
    }
}

static void refresh_runtime_labels_locked(void)
{
    char line[64];
    bool wifi_connected = wifi_manager_is_connected();

    if (s_wifi_value) {
        lv_label_set_text(s_wifi_value, wifi_connected ? "ONLINE" : "WAITING");
        lv_obj_set_style_text_color(
            s_wifi_value, wifi_connected ? lv_color_hex(0x0F7B6C) : lv_color_hex(0x915F18), 0
        );
    }

    if (s_ip_value) {
        snprintf(line, sizeof(line), "IP %s", wifi_connected ? wifi_manager_get_ip() : "--");
        lv_label_set_text(s_ip_value, line);
    }

    if (s_mem_value) {
        snprintf(
            line, sizeof(line), "RAM %uK   PSRAM %uK",
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U)
        );
        lv_label_set_text(s_mem_value, line);
    }
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_runtime_labels_locked();
}

static void build_screen_locked(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xF8F0DC), 0);
    lv_obj_set_style_bg_grad_color(s_screen, lv_color_hex(0xCBEFE7), 0);
    lv_obj_set_style_bg_grad_dir(s_screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *halo = lv_obj_create(s_screen);
    lv_obj_remove_style_all(halo);
    lv_obj_set_size(halo, 220, 220);
    lv_obj_align(halo, LV_ALIGN_TOP_MID, 0, -36);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(halo, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(halo, lv_color_hex(0xFFFDF8), 0);
    lv_obj_set_style_border_width(halo, 2, 0);
    lv_obj_set_style_border_color(halo, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *ring = lv_obj_create(s_screen);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 156, 156);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, -4);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 8, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x85D8CB), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_0, 0);

    lv_obj_t *brand_chip = lv_obj_create(s_screen);
    lv_obj_remove_style_all(brand_chip);
    lv_obj_set_size(brand_chip, 118, 34);
    lv_obj_align(brand_chip, LV_ALIGN_TOP_LEFT, 20, 18);
    lv_obj_set_style_radius(brand_chip, 17, 0);
    lv_obj_set_style_bg_color(brand_chip, lv_color_hex(0x24352F), 0);
    lv_obj_set_style_bg_opa(brand_chip, LV_OPA_COVER, 0);

    lv_obj_t *brand_label = lv_label_create(brand_chip);
    lv_label_set_text(brand_label, "MINICLAW");
    lv_obj_set_style_text_font(brand_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(brand_label, lv_color_hex(0xFFF8EA), 0);
    lv_obj_center(brand_label);

    lv_obj_t *mode_chip = lv_obj_create(s_screen);
    lv_obj_remove_style_all(mode_chip);
    lv_obj_set_size(mode_chip, 74, 34);
    lv_obj_align(mode_chip, LV_ALIGN_TOP_RIGHT, -20, 18);
    lv_obj_set_style_radius(mode_chip, 17, 0);
    lv_obj_set_style_bg_color(mode_chip, lv_color_hex(0xFFF7E7), 0);
    lv_obj_set_style_bg_opa(mode_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mode_chip, 1, 0);
    lv_obj_set_style_border_color(mode_chip, lv_color_hex(0xDCCFB8), 0);

    s_mode_value = lv_label_create(mode_chip);
    lv_obj_set_style_text_font(s_mode_value, &lv_font_montserrat_14, 0);
    lv_obj_center(s_mode_value);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "MimiClaw");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x213A33), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 120);

    lv_obj_t *subtitle = lv_label_create(s_screen);
    lv_label_set_text(subtitle, "EchoEar display online with LVGL");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x53645C), 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    create_status_card(s_screen, 200, lv_color_hex(0x85D8CB), "SYSTEM", &s_phase_value);
    create_status_card(s_screen, 264, lv_color_hex(0xFFC37A), "BLE CLI", &s_ble_value);
    create_status_card(s_screen, 328, lv_color_hex(0x5BC0B0), "NETWORK", &s_wifi_value);

    s_details_panel = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_details_panel);
    lv_obj_set_size(s_details_panel, 320, 44);
    lv_obj_align(s_details_panel, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(s_details_panel, LV_OPA_0, 0);

    s_ip_value = lv_label_create(s_details_panel);
    lv_label_set_text(s_ip_value, "IP --");
    lv_obj_set_style_text_font(s_ip_value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ip_value, lv_color_hex(0x5A544A), 0);
    lv_obj_align(s_ip_value, LV_ALIGN_TOP_LEFT, 0, 0);

    s_mem_value = lv_label_create(s_details_panel);
    lv_label_set_text(s_mem_value, "RAM --   PSRAM --");
    lv_obj_set_style_text_font(s_mem_value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_mem_value, lv_color_hex(0x5A544A), 0);
    lv_obj_align(s_mem_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    refresh_static_labels_locked();
    refresh_runtime_labels_locked();
}

esp_err_t config_screen_init(void)
{
    esp_err_t err;

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
    }

    lv_scr_load(s_screen);
    if (s_details_panel) {
        if (s_details_visible) {
            lv_obj_clear_flag(s_details_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_details_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    refresh_static_labels_locked();
    refresh_runtime_labels_locked();
    s_active = true;

    display_panel_lvgl_unlock();
    ESP_LOGI(TAG, "LVGL config screen ready");
    return ESP_OK;
}

void config_screen_set_phase(const char *phase)
{
    const char *next = (phase && phase[0]) ? phase : "BOOTING";
    snprintf(s_phase_text, sizeof(s_phase_text), "%s", next);

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
    s_details_visible = !s_details_visible;

    if (!display_panel_lvgl_is_ready() || !s_details_panel) {
        return;
    }
    if (!display_panel_lvgl_lock(100)) {
        return;
    }

    if (s_details_visible) {
        lv_obj_clear_flag(s_details_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_details_panel, LV_OBJ_FLAG_HIDDEN);
    }

    display_panel_lvgl_unlock();
}

bool config_screen_is_active(void)
{
    return s_active;
}

void config_screen_scroll_down(void)
{
    ESP_LOGI(TAG, "Config screen uses automatic refresh; scroll action is not mapped yet");
}
