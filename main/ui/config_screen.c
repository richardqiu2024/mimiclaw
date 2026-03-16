#include "ui/config_screen.h"

#include <stdint.h>
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

typedef enum {
    CONFIG_PAGE_HOME = 0,
    CONFIG_PAGE_NETWORK,
    CONFIG_PAGE_SYSTEM,
    CONFIG_PAGE_COUNT,
} config_page_t;

typedef struct {
    lv_obj_t *card;
    lv_obj_t *accent;
    lv_obj_t *title;
    lv_obj_t *value;
} stat_slot_t;

static const char *s_page_names[CONFIG_PAGE_COUNT] = {
    "HOME",
    "NETWORK",
    "SYSTEM",
};

static bool s_active = false;
static bool s_details_visible = false;
static bool s_ble_ready = false;
static bool s_agent_ready = false;
static char s_phase_text[32] = "BOOTING";
static config_page_t s_active_page = CONFIG_PAGE_HOME;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_mode_value = NULL;
static lv_obj_t *s_subtitle = NULL;
static lv_obj_t *s_page_buttons[CONFIG_PAGE_COUNT] = {NULL};
static lv_obj_t *s_page_button_labels[CONFIG_PAGE_COUNT] = {NULL};

static lv_obj_t *s_hero_card = NULL;
static lv_obj_t *s_hero_eyebrow = NULL;
static lv_obj_t *s_hero_primary = NULL;
static lv_obj_t *s_hero_secondary = NULL;
static lv_obj_t *s_hero_note = NULL;
static stat_slot_t s_stat_slots[4] = {0};
static lv_obj_t *s_action_button = NULL;
static lv_obj_t *s_action_button_label = NULL;

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

static void apply_dark_card_style(lv_obj_t *card)
{
    lv_obj_remove_style_all(card);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_80, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x17304A), 0);
    lv_obj_set_style_shadow_width(card, 18, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x020617), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
}

static lv_obj_t *create_stat_card(
    lv_obj_t *parent, lv_coord_t x, lv_coord_t y, stat_slot_t *slot
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

    if (slot != NULL) {
        slot->card = card;

        slot->accent = lv_obj_create(card);
        lv_obj_remove_style_all(slot->accent);
        lv_obj_set_size(slot->accent, 9, 9);
        lv_obj_align(slot->accent, LV_ALIGN_LEFT_MID, 14, 0);
        lv_obj_set_style_radius(slot->accent, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(slot->accent, lv_color_hex(0x5BC0EB), 0);
        lv_obj_set_style_bg_opa(slot->accent, LV_OPA_COVER, 0);

        slot->title = lv_label_create(card);
        lv_label_set_text(slot->title, "--");
        lv_obj_set_style_text_font(slot->title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(slot->title, lv_color_hex(0x8EA3AF), 0);
        lv_obj_align(slot->title, LV_ALIGN_LEFT_MID, 28, 0);

        slot->value = lv_label_create(card);
        lv_obj_set_width(slot->value, 66);
        lv_label_set_text(slot->value, "--");
        lv_obj_set_style_text_align(slot->value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(slot->value, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(slot->value, lv_color_hex(0xECF4F7), 0);
        lv_obj_align(slot->value, LV_ALIGN_RIGHT_MID, -14, 0);
    }

    return card;
}

static lv_obj_t *create_page_button(
    lv_obj_t *parent, const char *label_text, config_page_t page
)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_t *label = lv_label_create(button);

    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 96, 30);
    lv_obj_set_style_radius(button, 15, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x17304A), 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);

    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x8EA3AF), 0);
    lv_obj_center(label);

    s_page_buttons[page] = button;
    s_page_button_labels[page] = label;
    return button;
}

static void set_stat_slot(
    stat_slot_t *slot, const char *title, uint32_t accent_hex, const char *value, lv_color_t value_color
)
{
    if (slot == NULL) {
        return;
    }
    if (slot->title != NULL) {
        lv_label_set_text(slot->title, title);
    }
    if (slot->accent != NULL) {
        lv_obj_set_style_bg_color(slot->accent, lv_color_hex(accent_hex), 0);
    }
    if (slot->value != NULL) {
        lv_label_set_text(slot->value, value);
        lv_obj_set_style_text_color(slot->value, value_color, 0);
    }
}

static void set_page_button_state_locked(config_page_t page)
{
    for (int index = 0; index < CONFIG_PAGE_COUNT; ++index) {
        bool active = (index == (int)page);

        if (s_page_buttons[index] != NULL) {
            lv_obj_set_style_bg_color(
                s_page_buttons[index],
                active ? lv_color_hex(0xD9F0F2) : lv_color_hex(0x08131F),
                0
            );
            lv_obj_set_style_bg_opa(
                s_page_buttons[index],
                active ? LV_OPA_COVER : LV_OPA_70,
                0
            );
            lv_obj_set_style_border_color(
                s_page_buttons[index],
                active ? lv_color_hex(0xD9F0F2) : lv_color_hex(0x17304A),
                0
            );
        }
        if (s_page_button_labels[index] != NULL) {
            lv_obj_set_style_text_color(
                s_page_button_labels[index],
                active ? lv_color_hex(0x082032) : lv_color_hex(0x8EA3AF),
                0
            );
        }
    }
}

static void set_action_button_locked(bool visible, const char *label, bool enabled)
{
    if ((s_action_button == NULL) || (s_action_button_label == NULL)) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(s_action_button, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_action_button_label, label);
        lv_obj_set_style_bg_color(
            s_action_button, enabled ? lv_color_hex(0xD9F0F2) : lv_color_hex(0x203040), 0
        );
        lv_obj_set_style_bg_opa(s_action_button, enabled ? LV_OPA_COVER : LV_OPA_70, 0);
        lv_obj_set_style_border_color(
            s_action_button, enabled ? lv_color_hex(0xD9F0F2) : lv_color_hex(0x294458), 0
        );
        lv_obj_set_style_text_color(
            s_action_button_label, enabled ? lv_color_hex(0x082032) : lv_color_hex(0x9BB1BF), 0
        );
        if (enabled) {
            lv_obj_add_flag(s_action_button, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        } else {
            lv_obj_clear_flag(s_action_button, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        }
    } else {
        lv_obj_add_flag(s_action_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_action_button, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    }
}

static void refresh_mode_locked(void)
{
    if (s_mode_value == NULL) {
        return;
    }

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

static void refresh_home_page_locked(void)
{
    bool wifi_connected = wifi_manager_is_connected();
    uint32_t ram_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U);
    char ram_text[16];
    char line[128];

    lv_label_set_text(s_subtitle, "Startup overview");
    lv_label_set_text(s_hero_eyebrow, "SYSTEM PHASE");

    if (!s_details_visible) {
        lv_label_set_text(s_hero_primary, friendly_phase_text());
        lv_label_set_text(s_hero_secondary, summary_text(wifi_connected));
        lv_label_set_text(s_hero_note, "Tap card or action chip for details.");
        lv_obj_set_style_text_color(
            s_hero_primary, s_agent_ready ? lv_color_hex(0xF8FAFC) : lv_color_hex(0xE6EEF5), 0
        );
        set_action_button_locked(true, "SHOW DETAILS", true);
    } else {
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
        lv_label_set_text(s_hero_note, "Single BOOT: next page  Double: previous");
        lv_obj_set_style_text_color(s_hero_primary, lv_color_hex(0x7DD3FC), 0);
        set_action_button_locked(true, "BACK TO STATUS", true);
    }

    set_stat_slot(
        &s_stat_slots[0], "BLE CLI", 0x5BC0EB,
        s_ble_ready ? "READY" : "WAITING",
        s_ble_ready ? lv_color_hex(0x34D399) : lv_color_hex(0xCBD5E1)
    );
    set_stat_slot(&s_stat_slots[1], "WI-FI", 0x34D399, wifi_state_text(wifi_connected), wifi_state_color(wifi_connected));
    set_stat_slot(
        &s_stat_slots[2], "AGENT", 0xA78BFA,
        s_agent_ready ? "ONLINE" : (wifi_connected ? "STARTING" : "STANDBY"),
        agent_state_color(wifi_connected)
    );

    snprintf(ram_text, sizeof(ram_text), "%uK", (unsigned)ram_kb);
    set_stat_slot(&s_stat_slots[3], "RAM", 0xFBBF24, ram_text, ram_state_color(ram_kb));

    lv_obj_add_flag(s_hero_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
}

static void refresh_network_page_locked(void)
{
    bool wifi_connected = wifi_manager_is_connected();
    char line[160];

    lv_label_set_text(s_subtitle, "Connectivity status");
    lv_label_set_text(s_hero_eyebrow, "NETWORK OVERVIEW");
    lv_label_set_text(s_hero_primary, wifi_connected ? "Wi-Fi Connected" : friendly_phase_text());

    if (wifi_connected) {
        snprintf(line, sizeof(line), "IP %s\nBLE %s", wifi_manager_get_ip(), s_ble_ready ? "ready" : "waiting");
    } else {
        snprintf(line, sizeof(line), "Wi-Fi %s\nBLE %s", wifi_state_text(wifi_connected), s_ble_ready ? "ready" : "waiting");
    }
    lv_label_set_text(s_hero_secondary, line);

    if (s_agent_ready) {
        lv_label_set_text(s_hero_note, "Cloud services are online.");
    } else if (wifi_connected) {
        lv_label_set_text(s_hero_note, "Network is healthy. Agent startup is in progress.");
    } else if (phase_matches("SET WIFI IN CLI")) {
        lv_label_set_text(s_hero_note, "Use BLE CLI to finish Wi-Fi setup.");
    } else {
        lv_label_set_text(s_hero_note, "BLE setup works before Wi-Fi joins.");
    }

    set_stat_slot(&s_stat_slots[0], "WI-FI", 0x34D399, wifi_state_text(wifi_connected), wifi_state_color(wifi_connected));
    set_stat_slot(
        &s_stat_slots[1], "AGENT", 0xA78BFA,
        s_agent_ready ? "ONLINE" : (wifi_connected ? "PENDING" : "OFFLINE"),
        agent_state_color(wifi_connected)
    );
    set_stat_slot(
        &s_stat_slots[2], "BLE", 0x5BC0EB,
        s_ble_ready ? "READY" : "WAITING",
        s_ble_ready ? lv_color_hex(0x34D399) : lv_color_hex(0xCBD5E1)
    );
    set_stat_slot(
        &s_stat_slots[3], "PHASE", 0xF59E0B,
        phase_matches("SYSTEM READY") ? "READY" : "RUNNING",
        phase_matches("SYSTEM READY") ? lv_color_hex(0x34D399) : lv_color_hex(0x60A5FA)
    );

    set_action_button_locked(false, "", false);
    lv_obj_clear_flag(s_hero_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
}

static void refresh_system_page_locked(void)
{
    bool touch_ready = display_panel_touch_is_ready();
    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    bool interrupt_enabled = false;
    uint32_t ram_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U);
    uint32_t psram_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U);
    uint16_t rotation = display_panel_get_rotation_degrees();
    char line[160];
    char text[16];

    (void)display_panel_touch_get_flags(&swap_xy, &mirror_x, &mirror_y, &interrupt_enabled);

    lv_label_set_text(s_subtitle, "Board health");
    lv_label_set_text(s_hero_eyebrow, "SYSTEM HEALTH");
    snprintf(line, sizeof(line), "Internal RAM %uK", (unsigned)ram_kb);
    lv_label_set_text(s_hero_primary, line);

    snprintf(
        line, sizeof(line), "Touch %s, rot %u deg\nFlags s%d mx%d my%d",
        touch_ready ? "ready" : "offline",
        (unsigned)rotation,
        swap_xy ? 1 : 0,
        mirror_x ? 1 : 0,
        mirror_y ? 1 : 0
    );
    lv_label_set_text(s_hero_secondary, line);
    lv_label_set_text(s_hero_note, "Action chip or BOOT long opens touch calibration.");

    set_stat_slot(
        &s_stat_slots[0], "TOUCH", 0x34D399,
        touch_ready ? "READY" : "OFF",
        touch_ready ? lv_color_hex(0x34D399) : lv_color_hex(0xFB7185)
    );
    set_stat_slot(
        &s_stat_slots[1], "IRQ", 0x60A5FA,
        interrupt_enabled ? "ENABLED" : "POLL",
        interrupt_enabled ? lv_color_hex(0x60A5FA) : lv_color_hex(0xFBBF24)
    );

    snprintf(text, sizeof(text), "%u", (unsigned)rotation);
    set_stat_slot(&s_stat_slots[2], "ROT", 0xA78BFA, text, lv_color_hex(0xA78BFA));

    snprintf(text, sizeof(text), "%uK", (unsigned)psram_kb);
    set_stat_slot(&s_stat_slots[3], "PSRAM", 0xFBBF24, text, ram_state_color(psram_kb));

    set_action_button_locked(true, "TOUCH CAL", true);
    lv_obj_clear_flag(s_hero_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
}

static void refresh_runtime_labels_locked(void)
{
    refresh_mode_locked();
    set_page_button_state_locked(s_active_page);

    switch (s_active_page) {
    case CONFIG_PAGE_NETWORK:
        refresh_network_page_locked();
        break;
    case CONFIG_PAGE_SYSTEM:
        refresh_system_page_locked();
        break;
    case CONFIG_PAGE_HOME:
    default:
        refresh_home_page_locked();
        break;
    }
}

static void set_page_locked(config_page_t page)
{
    if (((int)page < 0) || (page >= CONFIG_PAGE_COUNT)) {
        return;
    }

    s_active_page = page;
    refresh_runtime_labels_locked();
}

static void cycle_page_locked(int delta)
{
    int next = ((int)s_active_page + delta + CONFIG_PAGE_COUNT) % CONFIG_PAGE_COUNT;
    set_page_locked((config_page_t)next);
}

static void open_touch_calibration_locked(void)
{
    s_active = false;
    if (touch_calibration_screen_show() != ESP_OK) {
        s_active = true;
    }
}

static void hero_card_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_active_page != CONFIG_PAGE_HOME) {
        return;
    }

    s_details_visible = !s_details_visible;
    refresh_runtime_labels_locked();
}

static void action_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_active_page == CONFIG_PAGE_HOME) {
        s_details_visible = !s_details_visible;
        refresh_runtime_labels_locked();
        return;
    }

    if (s_active_page == CONFIG_PAGE_SYSTEM) {
        open_touch_calibration_locked();
    }
}

static void page_nav_event_cb(lv_event_t *event)
{
    config_page_t page;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    page = (config_page_t)(uintptr_t)lv_event_get_user_data(event);
    set_page_locked(page);
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
        open_touch_calibration_locked();
        return;
    }

    if (event == SINGLE_CLICK) {
        cycle_page_locked(+1);
        return;
    }

    if (event == DOUBLE_CLICK) {
        cycle_page_locked(-1);
    }
}

static void build_screen_locked(void)
{
    lv_obj_t *halo;
    lv_obj_t *brand_chip;
    lv_obj_t *brand_label;
    lv_obj_t *mode_chip;
    lv_obj_t *title;
    lv_obj_t *nav_bar;

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x07111F), 0);
    lv_obj_set_style_bg_grad_color(s_screen, lv_color_hex(0x16324A), 0);
    lv_obj_set_style_bg_grad_dir(s_screen, LV_GRAD_DIR_VER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    halo = lv_obj_create(s_screen);
    lv_obj_remove_style_all(halo);
    lv_obj_set_size(halo, 210, 210);
    lv_obj_align(halo, LV_ALIGN_TOP_MID, 0, -72);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(halo, lv_color_hex(0x103B58), 0);
    lv_obj_set_style_bg_opa(halo, LV_OPA_40, 0);

    brand_chip = lv_obj_create(s_screen);
    lv_obj_remove_style_all(brand_chip);
    lv_obj_set_size(brand_chip, 112, 30);
    lv_obj_align(brand_chip, LV_ALIGN_TOP_LEFT, 18, 18);
    lv_obj_set_style_radius(brand_chip, 15, 0);
    lv_obj_set_style_bg_color(brand_chip, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_opa(brand_chip, LV_OPA_70, 0);
    lv_obj_set_style_border_width(brand_chip, 1, 0);
    lv_obj_set_style_border_color(brand_chip, lv_color_hex(0x17304A), 0);

    brand_label = lv_label_create(brand_chip);
    lv_label_set_text(brand_label, "MINICLAW");
    lv_obj_set_style_text_font(brand_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(brand_label, lv_color_hex(0xD7E7F3), 0);
    lv_obj_center(brand_label);

    mode_chip = lv_obj_create(s_screen);
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

    title = lv_label_create(s_screen);
    lv_label_set_text(title, "MimiClaw");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF7FAFC), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);

    s_subtitle = lv_label_create(s_screen);
    lv_label_set_text(s_subtitle, "Startup overview");
    lv_obj_set_style_text_font(s_subtitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_subtitle, lv_color_hex(0x90A8BC), 0);
    lv_obj_align_to(s_subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    nav_bar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(nav_bar);
    lv_obj_set_size(nav_bar, 312, 30);
    lv_obj_align(nav_bar, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_clear_flag(nav_bar, LV_OBJ_FLAG_SCROLLABLE);

    for (int index = 0; index < CONFIG_PAGE_COUNT; ++index) {
        lv_obj_t *button = create_page_button(nav_bar, s_page_names[index], (config_page_t)index);
        lv_obj_set_pos(button, index * 108, 0);
        lv_obj_add_event_cb(button, page_nav_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    }

    s_hero_card = lv_obj_create(s_screen);
    apply_dark_card_style(s_hero_card);
    lv_obj_set_size(s_hero_card, 300, 112);
    lv_obj_set_pos(s_hero_card, 30, 126);
    lv_obj_set_style_pad_left(s_hero_card, 20, 0);
    lv_obj_set_style_pad_right(s_hero_card, 20, 0);
    lv_obj_set_style_pad_top(s_hero_card, 18, 0);
    lv_obj_set_style_pad_bottom(s_hero_card, 18, 0);
    lv_obj_add_event_cb(s_hero_card, hero_card_event_cb, LV_EVENT_CLICKED, NULL);

    s_hero_eyebrow = lv_label_create(s_hero_card);
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

    create_stat_card(s_screen, 34, 248, &s_stat_slots[0]);
    create_stat_card(s_screen, 188, 248, &s_stat_slots[1]);
    create_stat_card(s_screen, 34, 298, &s_stat_slots[2]);
    create_stat_card(s_screen, 188, 298, &s_stat_slots[3]);

    s_action_button = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_action_button);
    lv_obj_set_size(s_action_button, 160, 26);
    lv_obj_align(s_action_button, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_radius(s_action_button, 14, 0);
    lv_obj_set_style_bg_color(s_action_button, lv_color_hex(0xD9F0F2), 0);
    lv_obj_set_style_bg_opa(s_action_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_action_button, 1, 0);
    lv_obj_set_style_border_color(s_action_button, lv_color_hex(0xD9F0F2), 0);
    lv_obj_add_flag(s_action_button, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_action_button, action_button_event_cb, LV_EVENT_CLICKED, NULL);

    s_action_button_label = lv_label_create(s_action_button);
    lv_label_set_text(s_action_button_label, "SHOW DETAILS");
    lv_obj_set_style_text_font(s_action_button_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_action_button_label, lv_color_hex(0x082032), 0);
    lv_obj_center(s_action_button_label);

    lv_obj_move_foreground(brand_chip);
    lv_obj_move_foreground(mode_chip);
    lv_obj_move_foreground(title);
    lv_obj_move_foreground(s_subtitle);
    lv_obj_move_foreground(nav_bar);

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
        refresh_runtime_labels_locked();
        display_panel_lvgl_unlock();
    }
}

void config_screen_set_ble_ready(bool ready)
{
    s_ble_ready = ready;

    if (display_panel_lvgl_is_ready() && display_panel_lvgl_lock(100)) {
        refresh_runtime_labels_locked();
        display_panel_lvgl_unlock();
    }
}

void config_screen_set_agent_ready(bool ready)
{
    s_agent_ready = ready;

    if (display_panel_lvgl_is_ready() && display_panel_lvgl_lock(100)) {
        refresh_runtime_labels_locked();
        display_panel_lvgl_unlock();
    }
}

bool config_screen_is_active(void)
{
    return s_active;
}
