#include "ui/config_screen.h"

#include <stdio.h>
#include <string.h>

#include "buttons/button_driver.h"
#include "display/display_panel.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui/touch_calibration_screen.h"
#include "wifi/wifi_manager.h"

static const char *TAG = "config_screen";

static bool s_active = false;
static bool s_details_visible = false;
static bool s_ble_ready = false;
static bool s_agent_ready = false;
static char s_phase_text[32] = "BOOTING";

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_mode_value = NULL;
static lv_obj_t *s_hero_card = NULL;
static lv_obj_t *s_hero_eyebrow = NULL;
static lv_obj_t *s_hero_primary = NULL;
static lv_obj_t *s_hero_secondary = NULL;
static lv_obj_t *s_hero_note = NULL;
static lv_obj_t *s_ble_value = NULL;
static lv_obj_t *s_wifi_value = NULL;
static lv_obj_t *s_agent_value = NULL;
static lv_obj_t *s_ram_value = NULL;
static lv_obj_t *s_details_button = NULL;
static lv_obj_t *s_details_button_label = NULL;
static lv_timer_t *s_status_timer = NULL;
static lv_timer_t *s_button_timer = NULL;

static bool phase_matches(const char *phase)
{
    return strcmp(s_phase_text, phase) == 0;
}

static const char *friendly_phase_text(void)
{
    if (phase_matches("CORE START")) {
        return "Starting Core";
    }
    if (phase_matches("SERVICES READY")) {
        return "Loading Services";
    }
    if (phase_matches("BLE READY")) {
        return "BLE Ready";
    }
    if (phase_matches("WIFI START")) {
        return "Joining Wi-Fi";
    }
    if (phase_matches("WIFI TIMEOUT")) {
        return "Wi-Fi Timeout";
    }
    if (phase_matches("SET WIFI IN CLI")) {
        return "Set Wi-Fi";
    }
    if (phase_matches("SYSTEM READY")) {
        return "System Ready";
    }
    return s_phase_text;
}

static const char *summary_text(bool wifi_connected)
{
    if (s_agent_ready) {
        return "Telegram and local services are online.";
    }
    if (wifi_connected) {
        return "Network is up. Finishing agent startup.";
    }
    if (phase_matches("SET WIFI IN CLI")) {
        return "Use BLE CLI to add Wi-Fi and API keys.";
    }
    if (phase_matches("WIFI TIMEOUT")) {
        return "Wi-Fi did not connect. Check credentials.";
    }
    if (phase_matches("BLE READY")) {
        return "BLE setup is ready before network startup.";
    }
    if (phase_matches("SERVICES READY")) {
        return "Core services loaded. Starting BLE and Wi-Fi.";
    }
    if (phase_matches("CORE START")) {
        return "Preparing storage and local runtime.";
    }
    if (phase_matches("WIFI START")) {
        return "Joining Wi-Fi and waiting for an IP.";
    }
    return "Pocket ESP32 agent with BLE and Wi-Fi runtime.";
}

static const char *wifi_state_text(bool wifi_connected)
{
    if (wifi_connected) {
        return "ONLINE";
    }
    if (phase_matches("SET WIFI IN CLI")) {
        return "SETUP";
    }
    if (phase_matches("WIFI TIMEOUT")) {
        return "TIMEOUT";
    }
    if (phase_matches("WIFI START")) {
        return "JOINING";
    }
    return "WAITING";
}

static lv_color_t wifi_state_color(bool wifi_connected)
{
    if (wifi_connected) {
        return lv_color_hex(0x34D399);
    }
    if (phase_matches("WIFI TIMEOUT")) {
        return lv_color_hex(0xFB7185);
    }
    if (phase_matches("SET WIFI IN CLI")) {
        return lv_color_hex(0xFBBF24);
    }
    return lv_color_hex(0xCBD5E1);
}

static lv_color_t agent_state_color(bool wifi_connected)
{
    if (s_agent_ready) {
        return lv_color_hex(0x34D399);
    }
    if (wifi_connected) {
        return lv_color_hex(0x60A5FA);
    }
    return lv_color_hex(0xCBD5E1);
}

static lv_color_t ram_state_color(uint32_t ram_kb)
{
    if (ram_kb >= 180U) {
        return lv_color_hex(0x34D399);
    }
    if (ram_kb >= 120U) {
        return lv_color_hex(0xFBBF24);
    }
    return lv_color_hex(0xFB7185);
}

static lv_obj_t *create_stat_card(
    lv_obj_t *parent, lv_coord_t x, lv_coord_t y, uint32_t accent_hex, const char *title, lv_obj_t **value_label
)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 138, 42);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_70, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x17304A), 0);

    lv_obj_t *accent = lv_obj_create(card);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 9, 9);
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_set_style_radius(accent, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(accent_hex), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x8EA3AF), 0);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 28, 0);

    lv_obj_t *value = lv_label_create(card);
    lv_obj_set_width(value, 66);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0xECF4F7), 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -14, 0);

    if (value_label != NULL) {
        *value_label = value;
    }
    return card;
}

static void set_status_value(lv_obj_t *label, const char *text, lv_color_t color)
{
    if (label == NULL) {
        return;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
}

static void set_details_visible_locked(bool visible)
{
    s_details_visible = visible;

    if (s_details_button_label != NULL) {
        lv_label_set_text(s_details_button_label, visible ? "BACK TO STATUS" : "SHOW DETAILS");
    }
}

static void toggle_details_locked(void)
{
    set_details_visible_locked(!s_details_visible);
}

static void refresh_hero_locked(void)
{
    bool wifi_connected = wifi_manager_is_connected();
    char line[128];

    if ((s_hero_eyebrow == NULL) || (s_hero_primary == NULL) ||
            (s_hero_secondary == NULL) || (s_hero_note == NULL)) {
        return;
    }

    if (!s_details_visible) {
        lv_label_set_text(s_hero_eyebrow, "SYSTEM PHASE");
        lv_label_set_text(s_hero_primary, friendly_phase_text());
        lv_label_set_text(s_hero_secondary, summary_text(wifi_connected));
        lv_label_set_text(s_hero_note, "Tap for details or press BOOT.");
        lv_obj_set_style_text_color(
            s_hero_primary, s_agent_ready ? lv_color_hex(0xF8FAFC) : lv_color_hex(0xE6EEF5), 0
        );
        return;
    }

    lv_label_set_text(s_hero_eyebrow, "DEVICE DETAILS");
    if (wifi_connected) {
        snprintf(line, sizeof(line), "IP %s", wifi_manager_get_ip());
    } else {
        snprintf(line, sizeof(line), "IP NOT READY");
    }
    lv_label_set_text(s_hero_primary, line);

    snprintf(
        line, sizeof(line), "RAM %uK\nPSRAM %uK",
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U)
    );
    lv_label_set_text(s_hero_secondary, line);

    if (s_agent_ready) {
        lv_label_set_text(s_hero_note, "Agent is live.");
    } else if (wifi_connected) {
        lv_label_set_text(s_hero_note, "Wi-Fi ready. Waiting for agent.");
    } else if (phase_matches("SET WIFI IN CLI")) {
        lv_label_set_text(s_hero_note, "Use BLE CLI to set Wi-Fi and tokens.");
    } else {
        lv_label_set_text(s_hero_note, "BLE CLI stays available.");
    }

    lv_obj_set_style_text_color(s_hero_primary, lv_color_hex(0x7DD3FC), 0);
}

static void refresh_static_labels_locked(void)
{
    bool wifi_connected = wifi_manager_is_connected();

    if (s_mode_value != NULL) {
        if (s_agent_ready) {
            lv_label_set_text(s_mode_value, "LIVE");
            lv_obj_set_style_text_color(s_mode_value, lv_color_hex(0x34D399), 0);
        } else if (s_ble_ready) {
            lv_label_set_text(s_mode_value, "SETUP");
            lv_obj_set_style_text_color(s_mode_value, lv_color_hex(0xFBBF24), 0);
        } else {
            lv_label_set_text(s_mode_value, "BOOT");
            lv_obj_set_style_text_color(s_mode_value, lv_color_hex(0xCBD5E1), 0);
        }
    }

    set_status_value(
        s_ble_value, s_ble_ready ? "READY" : "WAITING",
        s_ble_ready ? lv_color_hex(0x34D399) : lv_color_hex(0xCBD5E1)
    );
    set_status_value(s_wifi_value, wifi_state_text(wifi_connected), wifi_state_color(wifi_connected));
    set_status_value(
        s_agent_value, s_agent_ready ? "ONLINE" : (wifi_connected ? "STARTING" : "STANDBY"),
        agent_state_color(wifi_connected)
    );

    uint32_t ram_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U);
    char ram_text[16];
    snprintf(ram_text, sizeof(ram_text), "%uK", (unsigned)ram_kb);
    set_status_value(s_ram_value, ram_text, ram_state_color(ram_kb));

    refresh_hero_locked();
}

static void refresh_runtime_labels_locked(void)
{
    refresh_static_labels_locked();
}

static void details_toggle_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    toggle_details_locked();
    refresh_hero_locked();
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!s_active) {
        return;
    }

    refresh_runtime_labels_locked();
}

static void button_timer_cb(lv_timer_t *timer)
{
    PressEvent event = BOOT_KEY_State;

    (void)timer;

    if (!s_active || (event == NONE_PRESS)) {
        return;
    }
    BOOT_KEY_State = NONE_PRESS;

    if (event == LONG_PRESS_START) {
        s_active = false;
        if (touch_calibration_screen_show() != ESP_OK) {
            s_active = true;
        }
        return;
    }

    if ((event != SINGLE_CLICK) && (event != DOUBLE_CLICK)) {
        return;
    }

    toggle_details_locked();
    refresh_hero_locked();
}

static void build_screen_locked(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x07111F), 0);
    lv_obj_set_style_bg_grad_color(s_screen, lv_color_hex(0x16324A), 0);
    lv_obj_set_style_bg_grad_dir(s_screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *halo = lv_obj_create(s_screen);
    lv_obj_remove_style_all(halo);
    lv_obj_set_size(halo, 210, 210);
    lv_obj_align(halo, LV_ALIGN_TOP_MID, 0, -72);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(halo, lv_color_hex(0x103B58), 0);
    lv_obj_set_style_bg_opa(halo, LV_OPA_40, 0);

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
    lv_label_set_text(brand_label, "MINICLAW");
    lv_obj_set_style_text_font(brand_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(brand_label, lv_color_hex(0xD7E7F3), 0);
    lv_obj_center(brand_label);

    lv_obj_t *mode_chip = lv_obj_create(s_screen);
    lv_obj_remove_style_all(mode_chip);
    lv_obj_set_size(mode_chip, 76, 30);
    lv_obj_align(mode_chip, LV_ALIGN_TOP_RIGHT, -18, 18);
    lv_obj_set_style_radius(mode_chip, 15, 0);
    lv_obj_set_style_bg_color(mode_chip, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_opa(mode_chip, LV_OPA_70, 0);
    lv_obj_set_style_border_width(mode_chip, 1, 0);
    lv_obj_set_style_border_color(mode_chip, lv_color_hex(0x17304A), 0);

    s_mode_value = lv_label_create(mode_chip);
    lv_label_set_text(s_mode_value, "--");
    lv_obj_set_style_text_font(s_mode_value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_mode_value, lv_color_hex(0xECF4F7), 0);
    lv_obj_center(s_mode_value);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "MimiClaw");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF7FAFC), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t *subtitle = lv_label_create(s_screen);
    lv_label_set_text(subtitle, "ESP32 agent dashboard");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x90A8BC), 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    s_hero_card = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_hero_card);
    lv_obj_set_size(s_hero_card, 300, 118);
    lv_obj_align(s_hero_card, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_radius(s_hero_card, 26, 0);
    lv_obj_set_style_bg_color(s_hero_card, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_opa(s_hero_card, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_hero_card, 1, 0);
    lv_obj_set_style_border_color(s_hero_card, lv_color_hex(0x17304A), 0);
    lv_obj_set_style_shadow_width(s_hero_card, 18, 0);
    lv_obj_set_style_shadow_color(s_hero_card, lv_color_hex(0x020617), 0);
    lv_obj_set_style_shadow_opa(s_hero_card, LV_OPA_40, 0);
    lv_obj_set_style_pad_left(s_hero_card, 20, 0);
    lv_obj_set_style_pad_right(s_hero_card, 20, 0);
    lv_obj_set_style_pad_top(s_hero_card, 18, 0);
    lv_obj_set_style_pad_bottom(s_hero_card, 18, 0);
    lv_obj_add_flag(s_hero_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_hero_card, details_toggle_event_cb, LV_EVENT_CLICKED, NULL);

    s_hero_eyebrow = lv_label_create(s_hero_card);
    lv_label_set_text(s_hero_eyebrow, "SYSTEM PHASE");
    lv_obj_set_style_text_font(s_hero_eyebrow, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hero_eyebrow, lv_color_hex(0x7DD3FC), 0);
    lv_obj_align(s_hero_eyebrow, LV_ALIGN_TOP_LEFT, 0, 0);

    s_hero_primary = lv_label_create(s_hero_card);
    lv_obj_set_width(s_hero_primary, 260);
    lv_label_set_long_mode(s_hero_primary, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_hero_primary, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_hero_primary, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(s_hero_primary, LV_ALIGN_TOP_LEFT, 0, 20);

    s_hero_secondary = lv_label_create(s_hero_card);
    lv_obj_set_width(s_hero_secondary, 260);
    lv_label_set_long_mode(s_hero_secondary, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_hero_secondary, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hero_secondary, lv_color_hex(0xC9D8E4), 0);
    lv_obj_align(s_hero_secondary, LV_ALIGN_TOP_LEFT, 0, 46);

    s_hero_note = lv_label_create(s_hero_card);
    lv_obj_set_width(s_hero_note, 260);
    lv_label_set_long_mode(s_hero_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_hero_note, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hero_note, lv_color_hex(0x7F97AA), 0);
    lv_obj_align(s_hero_note, LV_ALIGN_TOP_LEFT, 0, 82);

    create_stat_card(s_screen, 34, 214, 0x5BC0EB, "BLE CLI", &s_ble_value);
    create_stat_card(s_screen, 188, 214, 0x34D399, "WI-FI", &s_wifi_value);
    create_stat_card(s_screen, 34, 264, 0xA78BFA, "AGENT", &s_agent_value);
    create_stat_card(s_screen, 188, 264, 0xFBBF24, "RAM", &s_ram_value);

    s_details_button = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_details_button);
    lv_obj_set_size(s_details_button, 160, 26);
    lv_obj_align(s_details_button, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_radius(s_details_button, 14, 0);
    lv_obj_set_style_bg_color(s_details_button, lv_color_hex(0xD9F0F2), 0);
    lv_obj_set_style_bg_opa(s_details_button, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_details_button, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_details_button, details_toggle_event_cb, LV_EVENT_CLICKED, NULL);

    s_details_button_label = lv_label_create(s_details_button);
    lv_label_set_text(s_details_button_label, "SHOW DETAILS");
    lv_obj_set_style_text_font(s_details_button_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_details_button_label, lv_color_hex(0x082032), 0);
    lv_obj_center(s_details_button_label);

    set_details_visible_locked(false);
    refresh_static_labels_locked();
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
        s_button_timer = lv_timer_create(button_timer_cb, 60, NULL);
    }

    lv_scr_load(s_screen);
    refresh_runtime_labels_locked();
    s_active = true;

    display_panel_lvgl_unlock();
    ESP_LOGI(TAG, "Dashboard UI ready");
    return ESP_OK;
}

void config_screen_set_phase(const char *phase)
{
    const char *next = (phase && phase[0] != '\0') ? phase : "BOOTING";
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

bool config_screen_is_active(void)
{
    return s_active;
}
