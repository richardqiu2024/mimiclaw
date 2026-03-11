#include "serial_cli.h"
#include "mimi_config.h"
#include "wifi/wifi_manager.h"
#include "telegram/telegram_bot.h"
#include "llm/llm_proxy.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "tools/tool_sdcard.h"
#include "tools/tool_web_search.h"
#include "cron/cron_service.h"
#include "heartbeat/heartbeat.h"
#include "skills/skill_loader.h"
#include "display/display_panel.h"
#include "ui/config_screen.h"
#include "ui/touch_calibration_screen.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netdb.h>
#include <lwip/inet.h>
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "argtable3/argtable3.h"
#include "cJSON.h"

static const char *TAG = "cli";
static bool s_cli_initialized = false;
static serial_cli_output_cb_t s_output_cb = NULL;
static void *s_output_ctx = NULL;

#define TOOL_DEBUG_OUTPUT_SIZE ((32 * 1024) + 1)

static void cli_write_raw(const char *data, size_t len)
{
    if (!data || len == 0) return;

    if (s_output_cb) {
        s_output_cb(data, len, s_output_ctx);
        return;
    }

    fwrite(data, 1, len, stdout);
    fflush(stdout);
}

static int cli_printf_impl(const char *fmt, ...)
{
    char stack_buf[256];

    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap);
    va_end(ap);

    if (needed <= 0) return needed;
    if ((size_t)needed < sizeof(stack_buf)) {
        cli_write_raw(stack_buf, (size_t)needed);
        return needed;
    }

    char *heap_buf = calloc(1, (size_t)needed + 1);
    if (!heap_buf) return -1;

    va_start(ap, fmt);
    vsnprintf(heap_buf, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    cli_write_raw(heap_buf, (size_t)needed);
    free(heap_buf);
    return needed;
}

static int cli_fputs_impl(const char *s, FILE *stream)
{
    (void)stream;
    if (!s) return EOF;
    size_t len = strlen(s);
    cli_write_raw(s, len);
    return (int)len;
}

static void cli_arg_print_errors_impl(const struct arg_end *end, const char *progname)
{
    (void)end;
    cli_printf_impl("Invalid arguments for '%s'.\n", progname ? progname : "command");
}

#define printf(...) cli_printf_impl(__VA_ARGS__)
#define fputs(s, stream) cli_fputs_impl((s), (stream))
#define arg_print_errors(stream, end, progname) cli_arg_print_errors_impl((end), (progname))

static bool json_number_literal_valid(const char *text)
{
    char *end = NULL;

    if ((text == NULL) || (text[0] == '\0')) {
        return false;
    }

    (void)strtod(text, &end);
    return (end != NULL) && (*end == '\0');
}

static char *copy_trimmed_span(const char *start, const char *end)
{
    while ((start < end) && isspace((unsigned char)*start)) {
        start++;
    }
    while ((end > start) && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    size_t len = (size_t)(end - start);
    char *copy = calloc(1, len + 1);
    if (copy == NULL) {
        return NULL;
    }

    if (len > 0) {
        memcpy(copy, start, len);
    }
    copy[len] = '\0';
    return copy;
}

static char *dup_trimmed_text(const char *text)
{
    if (text == NULL) {
        return NULL;
    }

    return copy_trimmed_span(text, text + strlen(text));
}

static void strip_matching_outer_quotes(char *text)
{
    if (text == NULL) {
        return;
    }

    while (true) {
        size_t len = strlen(text);

        if (len < 2) {
            return;
        }

        if (((text[0] == '\'') || (text[0] == '"')) && (text[len - 1] == text[0])) {
            memmove(text, text + 1, len - 2);
            text[len - 2] = '\0';
            continue;
        }

        return;
    }
}

static char *normalize_json_text(const char *text)
{
    cJSON *root = NULL;
    char *normalized = NULL;

    if ((text == NULL) || (text[0] == '\0')) {
        return NULL;
    }

    root = cJSON_Parse(text);
    if (root == NULL) {
        return NULL;
    }

    if (cJSON_IsString(root)) {
        const char *inner = cJSON_GetStringValue(root);
        char *trimmed_inner = dup_trimmed_text(inner);

        if ((trimmed_inner != NULL) && (trimmed_inner[0] != '\0')) {
            cJSON *inner_root = cJSON_Parse(trimmed_inner);
            if (inner_root != NULL) {
                normalized = cJSON_PrintUnformatted(inner_root);
                cJSON_Delete(inner_root);
                free(trimmed_inner);
                cJSON_Delete(root);
                return normalized;
            }
        }

        free(trimmed_inner);
    }

    normalized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return normalized;
}

static char *decode_wrapped_json_string(const char *text)
{
    size_t len = 0;
    char *wrapped = NULL;
    char *normalized = NULL;

    if ((text == NULL) || (text[0] == '\0')) {
        return NULL;
    }

    len = strlen(text);
    wrapped = calloc(1, len + 3);
    if (wrapped == NULL) {
        return NULL;
    }

    wrapped[0] = '"';
    memcpy(wrapped + 1, text, len);
    wrapped[len + 1] = '"';
    wrapped[len + 2] = '\0';

    normalized = normalize_json_text(wrapped);
    free(wrapped);
    return normalized;
}

static char *normalize_relaxed_tool_json(const char *input)
{
    const char *cursor = input;
    const char *body_end = NULL;
    cJSON *root = NULL;
    char *normalized = NULL;

    if (input == NULL) {
        return NULL;
    }

    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != '{') {
        return NULL;
    }

    body_end = cursor + strlen(cursor);
    while ((body_end > cursor) && isspace((unsigned char)*(body_end - 1))) {
        body_end--;
    }
    if ((body_end <= cursor) || (*(body_end - 1) != '}')) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cursor++;
    body_end--;

    while (cursor < body_end) {
        const char *key_start = NULL;
        const char *colon = NULL;
        const char *value_end = NULL;
        bool in_quote = false;
        char quote = '\0';
        int depth = 0;
        char *key = NULL;
        char *value = NULL;
        cJSON *json_value = NULL;

        while ((cursor < body_end) && (isspace((unsigned char)*cursor) || (*cursor == ','))) {
            cursor++;
        }
        if (cursor >= body_end) {
            break;
        }

        key_start = cursor;
        for (; cursor < body_end; ++cursor) {
            char ch = *cursor;
            if (in_quote) {
                if ((ch == quote) && ((cursor == key_start) || (*(cursor - 1) != '\\'))) {
                    in_quote = false;
                }
                continue;
            }
            if ((ch == '\'') || (ch == '"')) {
                in_quote = true;
                quote = ch;
                continue;
            }
            if ((ch == '{') || (ch == '[')) {
                depth++;
                continue;
            }
            if (((ch == '}') || (ch == ']')) && (depth > 0)) {
                depth--;
                continue;
            }
            if ((ch == ':') && (depth == 0)) {
                colon = cursor;
                break;
            }
        }

        if (colon == NULL) {
            cJSON_Delete(root);
            return NULL;
        }

        cursor = colon + 1;
        value_end = cursor;
        in_quote = false;
        quote = '\0';
        depth = 0;

        for (; value_end < body_end; ++value_end) {
            char ch = *value_end;
            if (in_quote) {
                if ((ch == quote) && ((value_end == cursor) || (*(value_end - 1) != '\\'))) {
                    in_quote = false;
                }
                continue;
            }
            if ((ch == '\'') || (ch == '"')) {
                in_quote = true;
                quote = ch;
                continue;
            }
            if ((ch == '{') || (ch == '[')) {
                depth++;
                continue;
            }
            if (((ch == '}') || (ch == ']')) && (depth > 0)) {
                depth--;
                continue;
            }
            if ((ch == ',') && (depth == 0)) {
                break;
            }
        }

        key = copy_trimmed_span(key_start, colon);
        value = copy_trimmed_span(cursor, value_end);
        if ((key == NULL) || (value == NULL) || (key[0] == '\0') || (value[0] == '\0')) {
            free(key);
            free(value);
            cJSON_Delete(root);
            return NULL;
        }

        strip_matching_outer_quotes(key);
        if (key[0] == '\0') {
            free(key);
            free(value);
            cJSON_Delete(root);
            return NULL;
        }

        if ((strcasecmp(value, "true") == 0) || (strcasecmp(value, "false") == 0)) {
            json_value = cJSON_CreateBool(strcasecmp(value, "true") == 0);
        } else if (strcasecmp(value, "null") == 0) {
            json_value = cJSON_CreateNull();
        } else if (json_number_literal_valid(value)) {
            json_value = cJSON_CreateNumber(strtod(value, NULL));
        } else {
            cJSON *parsed = cJSON_Parse(value);
            if (parsed != NULL) {
                json_value = parsed;
            } else {
                strip_matching_outer_quotes(value);
                json_value = cJSON_CreateString(value);
            }
        }

        if ((json_value == NULL) || !cJSON_AddItemToObject(root, key, json_value)) {
            free(key);
            free(value);
            if (json_value != NULL) {
                cJSON_Delete(json_value);
            }
            cJSON_Delete(root);
            return NULL;
        }

        free(key);
        free(value);
        cursor = value_end;
    }

    normalized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return normalized;
}

static const display_panel_touch_transform_t s_touch_transform_modes[] = {
    DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_XY,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_X,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_Y,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_XY,
    DISPLAY_PANEL_TOUCH_TRANSFORM_NONE,
    DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_X,
    DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_Y,
};

static const char *touch_transform_mode_name(display_panel_touch_transform_t mode)
{
    switch (mode) {
    case DISPLAY_PANEL_TOUCH_TRANSFORM_NONE:
        return "raw";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY:
        return "swap";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_X:
        return "swap_mx";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_Y:
        return "swap_my";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_XY:
        return "swap_mxy";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_X:
        return "mx";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_Y:
        return "my";
    case DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_XY:
        return "mxy";
    default:
        return "unknown";
    }
}

static bool touch_transform_mode_from_name(
    const char *name, display_panel_touch_transform_t *mode
)
{
    if ((name == NULL) || (mode == NULL)) {
        return false;
    }

    if ((strcasecmp(name, "raw") == 0) || (strcasecmp(name, "none") == 0)) {
        *mode = DISPLAY_PANEL_TOUCH_TRANSFORM_NONE;
        return true;
    }
    if (strcasecmp(name, "swap") == 0) {
        *mode = DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY;
        return true;
    }
    if ((strcasecmp(name, "swap_mx") == 0) || (strcasecmp(name, "swap+mx") == 0)) {
        *mode = DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_X;
        return true;
    }
    if ((strcasecmp(name, "swap_my") == 0) || (strcasecmp(name, "swap+my") == 0)) {
        *mode = DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_Y;
        return true;
    }
    if ((strcasecmp(name, "swap_mxy") == 0) || (strcasecmp(name, "swap+mxy") == 0)) {
        *mode = DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_XY;
        return true;
    }
    if ((strcasecmp(name, "mx") == 0) || (strcasecmp(name, "mirror_x") == 0)) {
        *mode = DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_X;
        return true;
    }
    if ((strcasecmp(name, "my") == 0) || (strcasecmp(name, "mirror_y") == 0)) {
        *mode = DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_Y;
        return true;
    }
    if ((strcasecmp(name, "mxy") == 0) || (strcasecmp(name, "mirror_xy") == 0)) {
        *mode = DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_XY;
        return true;
    }

    return false;
}

static void print_touch_cal_usage(void)
{
    printf("Usage:\n");
    printf("  touch_cal\n");
    printf("  touch_cal show\n");
    printf("  touch_cal dashboard\n");
    printf("  touch_cal status\n");
    printf("  touch_cal next\n");
    printf("  touch_cal prev\n");
    printf("  touch_cal mode <raw|swap|swap_mx|swap_my|swap_mxy|mx|my|mxy>\n");
}

static void print_touch_cal_status(void)
{
    printf("Touch ready: %s\n", display_panel_touch_is_ready() ? "yes" : "no");
    printf(
        "Touch calibration screen: %s\n",
        touch_calibration_screen_is_active() ? "active" : "inactive"
    );
    printf(
        "Touch transform mode: %s\n",
        touch_transform_mode_name(display_panel_touch_get_transform_mode())
    );
}

static int cmd_touch_cal(int argc, char **argv)
{
    esp_err_t err = ESP_OK;
    display_panel_touch_transform_t mode = DISPLAY_PANEL_TOUCH_TRANSFORM_NONE;

    if (argc <= 1 || (strcasecmp(argv[1], "show") == 0)) {
        err = touch_calibration_screen_show();
        if (err != ESP_OK) {
            printf("Failed to open touch calibration screen: %s\n", esp_err_to_name(err));
            return 1;
        }
        print_touch_cal_status();
        return 0;
    }

    if (strcasecmp(argv[1], "dashboard") == 0) {
        err = config_screen_init();
        if (err != ESP_OK) {
            printf("Failed to return to dashboard: %s\n", esp_err_to_name(err));
            return 1;
        }
        print_touch_cal_status();
        return 0;
    }

    if (strcasecmp(argv[1], "status") == 0) {
        print_touch_cal_status();
        return 0;
    }

    if ((strcasecmp(argv[1], "next") == 0) || (strcasecmp(argv[1], "prev") == 0)) {
        size_t count = sizeof(s_touch_transform_modes) / sizeof(s_touch_transform_modes[0]);
        display_panel_touch_transform_t current = display_panel_touch_get_transform_mode();
        size_t current_index = 0;
        int step = (strcasecmp(argv[1], "next") == 0) ? 1 : -1;

        for (size_t i = 0; i < count; ++i) {
            if (s_touch_transform_modes[i] == current) {
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

        err = display_panel_touch_set_transform_mode(s_touch_transform_modes[next_index]);
        if (err != ESP_OK) {
            printf("Failed to update touch transform mode: %s\n", esp_err_to_name(err));
            return 1;
        }

        if (touch_calibration_screen_is_active()) {
            (void)touch_calibration_screen_show();
        }
        print_touch_cal_status();
        return 0;
    }

    if (strcasecmp(argv[1], "mode") == 0) {
        if (argc < 3) {
            print_touch_cal_usage();
            return 1;
        }

        if (!touch_transform_mode_from_name(argv[2], &mode)) {
            printf("Unknown mode: %s\n", argv[2]);
            print_touch_cal_usage();
            return 1;
        }

        err = display_panel_touch_set_transform_mode(mode);
        if (err != ESP_OK) {
            printf("Failed to set touch transform mode: %s\n", esp_err_to_name(err));
            return 1;
        }

        if (touch_calibration_screen_is_active()) {
            (void)touch_calibration_screen_show();
        }
        print_touch_cal_status();
        return 0;
    }

    print_touch_cal_usage();
    return 1;
}

static int cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Available commands:\n");
    printf("  help\n");
    printf("  set_wifi <ssid> <password>\n");
    printf("  set_wifi_static <ip> <mask> <gateway> [dns1] [dns2]\n");
    printf("  clear_wifi_static\n");
    printf("  wifi_status\n");
    printf("  wifi_scan\n");
    printf("  net_test\n");
    printf("  set_tg_token <token>\n");
    printf("  set_api_key <key>\n");
    printf("  set_model <model>\n");
    printf("  set_model_provider <anthropic|openai|openai_compat|deepseek>\n");
    printf("  set_llm_base_url <https_url>\n");
    printf("  clear_llm_base_url\n");
    printf("  skill_list\n");
    printf("  skill_show <name>\n");
    printf("  skill_search <keyword>\n");
    printf("  memory_read\n");
    printf("  memory_write <content>\n");
    printf("  session_list\n");
    printf("  session_clear <chat_id>\n");
    printf("  heap_info\n");
    printf("  sd_status\n");
    printf("  sd_mount\n");
    printf("  set_search_key <key>\n");
    printf("  set_proxy <host> <port> [http|socks5]\n");
    printf("  clear_proxy\n");
    printf("  config_show\n");
    printf("  config_reset\n");
    printf("  heartbeat_trigger\n");
    printf("  cron_start\n");
    printf("  touch_cal [show|dashboard|status|next|prev|mode <name>]\n");
    printf("  tool_exec <name> [json]\n");
    printf("  tool_debug <list|show|exec> ...\n");
    printf("  restart\n");
    return 0;
}

/* --- wifi_set command --- */
static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_set_args;

static int cmd_wifi_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_set_args.end, argv[0]);
        return 1;
    }
    esp_err_t err = wifi_manager_set_credentials(wifi_set_args.ssid->sval[0],
                                                 wifi_set_args.password->sval[0]);
    if (err != ESP_OK) {
        printf("Failed to save WiFi credentials: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("WiFi credentials saved. Restart to apply.\n");
    return 0;
}

/* --- set_wifi_static command --- */
static struct {
    struct arg_str *ip;
    struct arg_str *mask;
    struct arg_str *gateway;
    struct arg_str *dns1;
    struct arg_str *dns2;
    struct arg_end *end;
} wifi_static_args;

static int cmd_set_wifi_static(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_static_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_static_args.end, argv[0]);
        return 1;
    }

    const char *dns1 = (wifi_static_args.dns1->count > 0) ? wifi_static_args.dns1->sval[0] : "";
    const char *dns2 = (wifi_static_args.dns2->count > 0) ? wifi_static_args.dns2->sval[0] : "";

    esp_err_t err = wifi_manager_set_static_ip(wifi_static_args.ip->sval[0],
                                               wifi_static_args.mask->sval[0],
                                               wifi_static_args.gateway->sval[0],
                                               dns1,
                                               dns2);
    if (err != ESP_OK) {
        printf("Failed to save static IP config: %s\n", esp_err_to_name(err));
        printf("Usage: set_wifi_static <ip> <mask> <gateway> [dns1] [dns2]\n");
        return 1;
    }

    printf("Static IP config saved. Restart to apply.\n");
    return 0;
}

/* --- clear_wifi_static command --- */
static int cmd_clear_wifi_static(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    esp_err_t err = wifi_manager_clear_static_ip();
    if (err != ESP_OK) {
        printf("Failed to clear static IP config: %s\n", esp_err_to_name(err));
        return 1;
    }

    printf("Static IP config cleared. Restart to apply DHCP.\n");
    return 0;
}

/* --- wifi_status command --- */
static int cmd_wifi_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("WiFi connected: %s\n", wifi_manager_is_connected() ? "yes" : "no");
    printf("IP: %s\n", wifi_manager_get_ip());

    bool static_enabled = wifi_manager_static_ip_enabled();
    printf("Static IP mode: %s\n", static_enabled ? "enabled" : "disabled");
    if (static_enabled) {
        char ip[16] = {0};
        char mask[16] = {0};
        char gateway[16] = {0};
        char dns1[16] = {0};
        char dns2[16] = {0};
        esp_err_t err = wifi_manager_get_static_ip(ip, sizeof(ip),
                                                   mask, sizeof(mask),
                                                   gateway, sizeof(gateway),
                                                   dns1, sizeof(dns1),
                                                   dns2, sizeof(dns2));
        if (err == ESP_OK) {
            printf("Static IP: %s\n", ip);
            printf("Static Mask: %s\n", mask);
            printf("Static Gateway: %s\n", gateway);
            printf("Static DNS1: %s\n", dns1[0] ? dns1 : "(empty)");
            printf("Static DNS2: %s\n", dns2[0] ? dns2 : "(empty)");
        } else {
            printf("Static config read failed: %s\n", esp_err_to_name(err));
        }
    }
    return 0;
}

/* --- set_tg_token command --- */
static struct {
    struct arg_str *token;
    struct arg_end *end;
} tg_token_args;

static int cmd_set_tg_token(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&tg_token_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, tg_token_args.end, argv[0]);
        return 1;
    }
    telegram_set_token(tg_token_args.token->sval[0]);
    printf("Telegram bot token saved.\n");
    return 0;
}

/* --- set_api_key command --- */
static struct {
    struct arg_str *key;
    struct arg_end *end;
} api_key_args;

static int cmd_set_api_key(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&api_key_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, api_key_args.end, argv[0]);
        return 1;
    }
    esp_err_t err = llm_set_api_key(api_key_args.key->sval[0]);
    if (err != ESP_OK) {
        printf("Failed to set API key: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("API key saved.\n");
    return 0;
}

/* --- set_model command --- */
static struct {
    struct arg_str *model;
    struct arg_end *end;
} model_args;

static int cmd_set_model(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&model_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, model_args.end, argv[0]);
        return 1;
    }
    esp_err_t err = llm_set_model(model_args.model->sval[0]);
    if (err != ESP_OK) {
        printf("Failed to set model: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Model set.\n");
    return 0;
}

/* --- set_model_provider command --- */
static struct {
    struct arg_str *provider;
    struct arg_end *end;
} provider_args;

static int cmd_set_model_provider(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&provider_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, provider_args.end, argv[0]);
        return 1;
    }
    esp_err_t err = llm_set_provider(provider_args.provider->sval[0]);
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_ARG) {
            printf("Invalid provider. Use anthropic | openai | openai_compat | deepseek\n");
        } else {
            printf("Failed to set provider: %s\n", esp_err_to_name(err));
        }
        return 1;
    }
    printf("Model provider set.\n");
    return 0;
}

/* --- set_llm_base_url command --- */
static struct {
    struct arg_str *url;
    struct arg_end *end;
} llm_base_url_args;

static int cmd_set_llm_base_url(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&llm_base_url_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, llm_base_url_args.end, argv[0]);
        return 1;
    }

    esp_err_t err = llm_set_base_url(llm_base_url_args.url->sval[0]);
    if (err != ESP_OK) {
        printf("Invalid URL. Must be HTTPS full path, e.g. https://host/v1/chat/completions\n");
        return 1;
    }

    printf("LLM base URL saved.\n");
    return 0;
}

/* --- clear_llm_base_url command --- */
static int cmd_clear_llm_base_url(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    esp_err_t err = llm_clear_base_url();
    if (err != ESP_OK) {
        printf("Failed to clear LLM base URL: %s\n", esp_err_to_name(err));
        return 1;
    }

    printf("LLM base URL cleared. Provider default endpoint will be used.\n");
    return 0;
}

/* --- memory_read command --- */
static int cmd_memory_read(int argc, char **argv)
{
    char *buf = malloc(4096);
    if (!buf) {
        printf("Out of memory.\n");
        return 1;
    }
    if (memory_read_long_term(buf, 4096) == ESP_OK && buf[0]) {
        printf("=== MEMORY.md ===\n%s\n=================\n", buf);
    } else {
        printf("MEMORY.md is empty or not found.\n");
    }
    free(buf);
    return 0;
}

/* --- memory_write command --- */
static struct {
    struct arg_str *content;
    struct arg_end *end;
} memory_write_args;

static int cmd_memory_write(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&memory_write_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, memory_write_args.end, argv[0]);
        return 1;
    }
    memory_write_long_term(memory_write_args.content->sval[0]);
    printf("MEMORY.md updated.\n");
    return 0;
}

/* --- session_list command --- */
static int cmd_session_list(int argc, char **argv)
{
    session_list();
    printf("Session list printed to ESP_LOG serial output.\n");
    return 0;
}

/* --- session_clear command --- */
static struct {
    struct arg_str *chat_id;
    struct arg_end *end;
} session_clear_args;

static int cmd_session_clear(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&session_clear_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, session_clear_args.end, argv[0]);
        return 1;
    }
    if (session_clear(session_clear_args.chat_id->sval[0]) == ESP_OK) {
        printf("Session cleared.\n");
    } else {
        printf("Session not found.\n");
    }
    return 0;
}

/* --- heap_info command --- */
static int cmd_heap_info(int argc, char **argv)
{
    printf("Internal free: %d bytes\n",
           (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    printf("PSRAM free:    %d bytes\n",
           (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    printf("Total free:    %d bytes\n",
           (int)esp_get_free_heap_size());
    return 0;
}

static int cmd_sd_status(int argc, char **argv)
{
    char status[384];
    esp_err_t err = tool_sdcard_get_status(status, sizeof(status));
    (void)argc;
    (void)argv;

    printf("%s", status[0] ? status : "(empty)\n");
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? 0 : 1;
}

static int cmd_sd_mount(int argc, char **argv)
{
    char status[384];
    esp_err_t err = tool_sdcard_mount();
    esp_err_t status_err;
    (void)argc;
    (void)argv;

    if (err != ESP_OK) {
        printf("SD mount failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    printf("SD mounted.\n");
    status_err = tool_sdcard_get_status(status, sizeof(status));
    if ((status_err == ESP_OK) || (status_err == ESP_ERR_NOT_FOUND)) {
        printf("%s", status[0] ? status : "(empty)\n");
    }
    return 0;
}

/* --- set_proxy command --- */
static struct {
    struct arg_str *host;
    struct arg_int *port;
    struct arg_str *type;
    struct arg_end *end;
} proxy_args;

static int cmd_set_proxy(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&proxy_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, proxy_args.end, argv[0]);
        return 1;
    }
    const char *proxy_type = "http";
    if (proxy_args.type->count > 0 && proxy_args.type->sval[0] && proxy_args.type->sval[0][0]) {
        proxy_type = proxy_args.type->sval[0];
    }
    if (strcmp(proxy_type, "http") != 0 && strcmp(proxy_type, "socks5") != 0) {
        printf("Invalid proxy type: %s. Use http or socks5.\n", proxy_type);
        return 1;
    }

    http_proxy_set(proxy_args.host->sval[0], (uint16_t)proxy_args.port->ival[0], proxy_type);
    printf("Proxy set. Restart to apply.\n");
    return 0;
}

/* --- clear_proxy command --- */
static int cmd_clear_proxy(int argc, char **argv)
{
    http_proxy_clear();
    printf("Proxy cleared. Restart to apply.\n");
    return 0;
}

/* --- set_search_key command --- */
static struct {
    struct arg_str *key;
    struct arg_end *end;
} search_key_args;

static int cmd_set_search_key(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&search_key_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, search_key_args.end, argv[0]);
        return 1;
    }
    tool_web_search_set_key(search_key_args.key->sval[0]);
    printf("Search API key saved (currently unused by local SearXNG backend).\n");
    return 0;
}

/* --- wifi_scan command --- */
static int cmd_wifi_scan(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    wifi_manager_scan_and_print();
    printf("WiFi scan completed. AP details are in ESP_LOG serial output.\n");
    return 0;
}

static int dns_lookup_ipv4(const char *host, char *out_ip, size_t out_ip_len)
{
    if (!host || !out_ip || out_ip_len == 0) {
        return -1;
    }

    out_ip[0] = '\0';

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, "443", &hints, &res);
    if (gai != 0 || !res) {
        if (res) {
            freeaddrinfo(res);
        }
        return (gai != 0) ? gai : -1;
    }

    const struct sockaddr_in *addr = (const struct sockaddr_in *)res->ai_addr;
    if (!addr || !inet_ntop(AF_INET, &addr->sin_addr, out_ip, (socklen_t)out_ip_len)) {
        out_ip[0] = '\0';
    }

    freeaddrinfo(res);
    return 0;
}

static esp_err_t https_head_direct(const char *url, int timeout_ms, int *out_status)
{
    if (out_status) {
        *out_status = 0;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_HEAD,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK && out_status) {
        *out_status = esp_http_client_get_status_code(client);
    }

    esp_http_client_cleanup(client);
    return err;
}

/* --- net_test command --- */
static int cmd_net_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    bool all_ok = true;
    char ip[16] = {0};

    printf("Network test (direct DNS + HTTPS):\n");
    if (http_proxy_is_enabled()) {
        printf("Proxy is enabled; this command still checks direct path.\n");
    }

    int gai = dns_lookup_ipv4("www.google.com", ip, sizeof(ip));
    if (gai == 0) {
        printf("[OK]   DNS www.google.com -> %s\n", ip[0] ? ip : "(ip unavailable)");
    } else {
        printf("[FAIL] DNS www.google.com failed (getaddrinfo=%d)\n", gai);
        all_ok = false;
    }

    int status = 0;
    esp_err_t err = https_head_direct("https://www.google.com/generate_204", 10000, &status);
    if (err == ESP_OK) {
        printf("[OK]   HTTPS https://www.google.com/generate_204 status=%d\n", status);
    } else {
        printf("[FAIL] HTTPS https://www.google.com/generate_204 failed (%s)\n", esp_err_to_name(err));
        all_ok = false;
    }

    gai = dns_lookup_ipv4("api.telegram.org", ip, sizeof(ip));
    if (gai == 0) {
        printf("[OK]   DNS api.telegram.org -> %s\n", ip[0] ? ip : "(ip unavailable)");
    } else {
        printf("[FAIL] DNS api.telegram.org failed (getaddrinfo=%d)\n", gai);
        all_ok = false;
    }

    status = 0;
    err = https_head_direct("https://api.telegram.org/", 10000, &status);
    if (err == ESP_OK) {
        printf("[OK]   HTTPS https://api.telegram.org/ status=%d\n", status);
    } else {
        printf("[FAIL] HTTPS https://api.telegram.org/ failed (%s)\n", esp_err_to_name(err));
        all_ok = false;
    }

    if (!all_ok) {
        printf("Hint: if static IP mode is enabled, set DNS1/DNS2 or run clear_wifi_static then restart.\n");
    }

    return all_ok ? 0 : 1;
}

/* --- skill_list command --- */
static int cmd_skill_list(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char *buf = malloc(4096);
    if (!buf) {
        printf("Out of memory.\n");
        return 1;
    }

    size_t n = skill_loader_build_summary(buf, 4096);
    if (n == 0) {
        printf("No skills found under /spiffs/skills/.\n");
    } else {
        printf("=== Skills ===\n%s", buf);
    }
    free(buf);
    return 0;
}

/* --- skill_show command --- */
static struct {
    struct arg_str *name;
    struct arg_end *end;
} skill_show_args;

static bool has_md_suffix(const char *name)
{
    size_t len = strlen(name);
    return (len >= 3) && strcmp(name + len - 3, ".md") == 0;
}

static bool build_skill_path(const char *name, char *out, size_t out_size)
{
    if (!name || !name[0]) return false;
    if (strstr(name, "..") != NULL) return false;
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) return false;

    if (has_md_suffix(name)) {
        snprintf(out, out_size, "/spiffs/skills/%s", name);
    } else {
        snprintf(out, out_size, "/spiffs/skills/%s.md", name);
    }
    return true;
}

static int cmd_skill_show(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&skill_show_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, skill_show_args.end, argv[0]);
        return 1;
    }

    char path[128];
    if (!build_skill_path(skill_show_args.name->sval[0], path, sizeof(path))) {
        printf("Invalid skill name.\n");
        return 1;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("Skill not found: %s\n", path);
        return 1;
    }

    printf("=== %s ===\n", path);
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        fputs(line, stdout);
    }
    fclose(f);
    printf("\n============\n");
    return 0;
}

/* --- skill_search command --- */
static struct {
    struct arg_str *keyword;
    struct arg_end *end;
} skill_search_args;

static bool contains_nocase(const char *text, const char *keyword)
{
    if (!text || !keyword || !keyword[0]) return false;

    size_t key_len = strlen(keyword);
    for (const char *p = text; *p; p++) {
        size_t i = 0;
        while (i < key_len && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)keyword[i])) {
            i++;
        }
        if (i == key_len) return true;
    }
    return false;
}

static int cmd_skill_search(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&skill_search_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, skill_search_args.end, argv[0]);
        return 1;
    }

    const char *keyword = skill_search_args.keyword->sval[0];
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        printf("Cannot open /spiffs.\n");
        return 1;
    }

    const char *prefix = "skills/";
    const size_t prefix_len = strlen(prefix);
    int matches = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        size_t name_len = strlen(name);

        if (strncmp(name, prefix, prefix_len) != 0) continue;
        if (name_len < prefix_len + 4) continue;
        if (strcmp(name + name_len - 3, ".md") != 0) continue;

        char full_path[296];
        snprintf(full_path, sizeof(full_path), "/spiffs/%s", name);

        bool file_matched = contains_nocase(name, keyword);
        int matched_line = 0;

        FILE *f = fopen(full_path, "r");
        if (!f) continue;

        char line[256];
        int line_no = 0;
        while (!file_matched && fgets(line, sizeof(line), f)) {
            line_no++;
            if (contains_nocase(line, keyword)) {
                file_matched = true;
                matched_line = line_no;
            }
        }
        fclose(f);

        if (file_matched) {
            matches++;
            if (matched_line > 0) {
                printf("- %s (matched at line %d)\n", full_path, matched_line);
            } else {
                printf("- %s (matched in filename)\n", full_path);
            }
        }
    }

    closedir(dir);
    if (matches == 0) {
        printf("No skills matched keyword: %s\n", keyword);
    } else {
        printf("Total matches: %d\n", matches);
    }
    return 0;
}

/* --- config_show command --- */
static void print_config(const char *label, const char *ns, const char *key,
                         const char *build_val, bool mask)
{
    char nvs_val[128] = {0};
    const char *source = "not set";
    const char *display = "(empty)";

    /* NVS takes highest priority */
    nvs_handle_t nvs;
    if (nvs_open(ns, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(nvs_val);
        if (nvs_get_str(nvs, key, nvs_val, &len) == ESP_OK && nvs_val[0]) {
            source = "NVS";
            display = nvs_val;
        }
        nvs_close(nvs);
    }

    /* Fall back to build-time value */
    if (strcmp(source, "not set") == 0 && build_val[0] != '\0') {
        source = "build";
        display = build_val;
    }

    if (mask && strlen(display) > 6 && strcmp(display, "(empty)") != 0) {
        printf("  %-14s: %.4s****  [%s]\n", label, display, source);
    } else {
        printf("  %-14s: %s  [%s]\n", label, display, source);
    }
}

static void print_wifi_static_enable_config(void)
{
    const char *source = "default";
    const char *display = "disabled";

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_WIFI, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t enabled = 0;
        if (nvs_get_u8(nvs, MIMI_NVS_KEY_WIFI_STATIC_EN, &enabled) == ESP_OK) {
            source = "NVS";
            display = enabled ? "enabled" : "disabled";
        }
        nvs_close(nvs);
    }

    printf("  %-14s: %s  [%s]\n", "WiFi Static", display, source);
}

static int cmd_config_show(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("=== Current Configuration ===\n");
    print_config("WiFi SSID",  MIMI_NVS_WIFI,   MIMI_NVS_KEY_SSID,     MIMI_SECRET_WIFI_SSID,  false);
    print_config("WiFi Pass",  MIMI_NVS_WIFI,   MIMI_NVS_KEY_PASS,     MIMI_SECRET_WIFI_PASS,  true);
    print_wifi_static_enable_config();
    print_config("WiFi IP",    MIMI_NVS_WIFI,   MIMI_NVS_KEY_WIFI_IP,   "", false);
    print_config("WiFi Mask",  MIMI_NVS_WIFI,   MIMI_NVS_KEY_WIFI_MASK, "", false);
    print_config("WiFi GW",    MIMI_NVS_WIFI,   MIMI_NVS_KEY_WIFI_GW,   "", false);
    print_config("WiFi DNS1",  MIMI_NVS_WIFI,   MIMI_NVS_KEY_WIFI_DNS1, "", false);
    print_config("WiFi DNS2",  MIMI_NVS_WIFI,   MIMI_NVS_KEY_WIFI_DNS2, "", false);
    print_config("TG Token",   MIMI_NVS_TG,     MIMI_NVS_KEY_TG_TOKEN, MIMI_SECRET_TG_TOKEN,   true);
    print_config("API Key",    MIMI_NVS_LLM,    MIMI_NVS_KEY_API_KEY,  MIMI_SECRET_API_KEY,    true);
    print_config("Model",      MIMI_NVS_LLM,    MIMI_NVS_KEY_MODEL,    MIMI_SECRET_MODEL,      false);
    print_config("Provider",   MIMI_NVS_LLM,    MIMI_NVS_KEY_PROVIDER, MIMI_SECRET_MODEL_PROVIDER, false);
    print_config("LLM URL",    MIMI_NVS_LLM,    MIMI_NVS_KEY_BASE_URL, "", false);
    print_config("Proxy Host", MIMI_NVS_PROXY,  MIMI_NVS_KEY_PROXY_HOST, MIMI_SECRET_PROXY_HOST, false);
    print_config("Proxy Port", MIMI_NVS_PROXY,  MIMI_NVS_KEY_PROXY_PORT, MIMI_SECRET_PROXY_PORT, false);
    print_config("Search Key", MIMI_NVS_SEARCH, MIMI_NVS_KEY_API_KEY,  MIMI_SECRET_SEARCH_KEY, true);
    printf("=============================\n");
    return 0;
}

/* --- config_reset command --- */
static int cmd_config_reset(int argc, char **argv)
{
    const char *namespaces[] = {
        MIMI_NVS_WIFI, MIMI_NVS_TG, MIMI_NVS_LLM, MIMI_NVS_PROXY, MIMI_NVS_SEARCH
    };
    for (int i = 0; i < 5; i++) {
        nvs_handle_t nvs;
        if (nvs_open(namespaces[i], NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_erase_all(nvs);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
    printf("All NVS config cleared. Build-time defaults will be used on restart.\n");
    return 0;
}

/* --- heartbeat_trigger command --- */
static int cmd_heartbeat_trigger(int argc, char **argv)
{
    printf("Checking HEARTBEAT.md...\n");
    if (heartbeat_trigger()) {
        printf("Heartbeat: agent prompted with pending tasks.\n");
    } else {
        printf("Heartbeat: no actionable tasks found.\n");
    }
    return 0;
}

/* --- cron_start command --- */
static int cmd_cron_start(int argc, char **argv)
{
    esp_err_t err = cron_service_start();
    if (err == ESP_OK) {
        printf("Cron service started.\n");
        return 0;
    }

    printf("Failed to start cron service: %s\n", esp_err_to_name(err));
    return 1;
}

static char *build_tool_exec_input_json(int argc, char **argv, int json_arg_index)
{
    size_t total_len = 3;  /* "{}" + '\0' */
    char *raw = NULL;
    char *trimmed = NULL;
    char *stripped = NULL;
    char *normalized = NULL;
    char *fallback = NULL;
    size_t off = 0;

    if (argc <= json_arg_index) {
        raw = calloc(1, total_len);
        if (raw != NULL) {
            memcpy(raw, "{}", 3);
        }
        return raw;
    }

    for (int i = json_arg_index; i < argc; ++i) {
        total_len += strlen(argv[i]) + 1;
    }

    raw = calloc(1, total_len);
    if (raw == NULL) {
        return NULL;
    }

    for (int i = json_arg_index; i < argc; ++i) {
        size_t part_len = strlen(argv[i]);
        memcpy(raw + off, argv[i], part_len);
        off += part_len;
        if (i < (argc - 1)) {
            raw[off++] = ' ';
        }
    }
    raw[off] = '\0';

    trimmed = dup_trimmed_text(raw);
    free(raw);
    if (trimmed == NULL) {
        return NULL;
    }
    if (trimmed[0] == '\0') {
        free(trimmed);
        raw = calloc(1, total_len);
        if (raw != NULL) {
            memcpy(raw, "{}", 3);
        }
        return raw;
    }

    normalized = normalize_json_text(trimmed);
    if (normalized != NULL) {
        if (normalized[0] != '"') {
            free(trimmed);
            return normalized;
        }
        fallback = normalized;
        normalized = NULL;
    }

    stripped = strdup(trimmed);
    if (stripped == NULL) {
        free(fallback);
        free(trimmed);
        return NULL;
    }
    strip_matching_outer_quotes(stripped);

    normalized = normalize_json_text(stripped);
    if (normalized != NULL) {
        if (normalized[0] != '"') {
            free(fallback);
            free(trimmed);
            free(stripped);
            return normalized;
        }
        free(fallback);
        fallback = normalized;
        normalized = NULL;
    }

    normalized = decode_wrapped_json_string(stripped);
    if (normalized != NULL) {
        if (normalized[0] != '"') {
            free(fallback);
            free(trimmed);
            free(stripped);
            return normalized;
        }
        free(fallback);
        fallback = normalized;
        normalized = NULL;
    }

    normalized = normalize_relaxed_tool_json(stripped);
    if (normalized != NULL) {
        free(fallback);
        free(trimmed);
        free(stripped);
        return normalized;
    }

    free(trimmed);
    if (fallback != NULL) {
        free(stripped);
        return fallback;
    }

    return stripped;
}

static cJSON *tool_debug_find_entry(cJSON *tools, const char *name)
{
    cJSON *tool = NULL;

    if (!cJSON_IsArray(tools) || (name == NULL) || (name[0] == '\0')) {
        return NULL;
    }

    cJSON_ArrayForEach(tool, tools) {
        const char *tool_name = cJSON_GetStringValue(cJSON_GetObjectItem(tool, "name"));
        if ((tool_name != NULL) && (strcmp(tool_name, name) == 0)) {
            return tool;
        }
    }

    return NULL;
}

static void print_tool_debug_usage(void)
{
    printf("Usage:\n");
    printf("  tool_debug list\n");
    printf("  tool_debug show <name>\n");
    printf("  tool_debug exec <name> [json]\n");
    printf("Examples:\n");
    printf("  tool_debug list\n");
    printf("  tool_debug show read_sd_file\n");
    printf("  tool_debug exec list_sd_dir '{\"path\":\"/sdcard\"}'\n");
    printf("  tool_debug exec read_sd_file '{\"path\":\"/sdcard/test.txt\",\"max_bytes\":256}'\n");
}

static int run_tool_exec_command(const char *tool_name, int argc, char **argv, int json_arg_index, bool verbose)
{
    char *input_json = NULL;
    char *output = NULL;
    int64_t start_us = 0;
    int64_t elapsed_us = 0;
    esp_err_t err;

    if ((tool_name == NULL) || (tool_name[0] == '\0')) {
        if (verbose) {
            print_tool_debug_usage();
        } else {
            printf("Usage: tool_exec <name> [json]\n");
        }
        return 1;
    }

    output = calloc(1, TOOL_DEBUG_OUTPUT_SIZE);
    if (!output) {
        printf("Out of memory.\n");
        return 1;
    }

    input_json = build_tool_exec_input_json(argc, argv, json_arg_index);
    if (!input_json) {
        printf("Out of memory.\n");
        free(output);
        return 1;
    }

    if (verbose) {
        printf("tool: %s\n", tool_name);
        printf("input: %s\n", input_json);
        printf("output_buffer: %d bytes\n", TOOL_DEBUG_OUTPUT_SIZE - 1);
    }

    start_us = esp_timer_get_time();
    err = tool_registry_execute(tool_name, input_json, output, TOOL_DEBUG_OUTPUT_SIZE);
    elapsed_us = esp_timer_get_time() - start_us;

    printf("%s status: %s\n", verbose ? "tool_debug" : "tool_exec", esp_err_to_name(err));
    if (verbose) {
        printf("elapsed_ms: %lld\n", (long long)(elapsed_us / 1000));
    }
    printf("%s\n", output[0] ? output : "(empty)");
    free(input_json);
    free(output);
    return (err == ESP_OK) ? 0 : 1;
}

static int cmd_tool_exec(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: tool_exec <name> [json]\n");
        return 1;
    }

    return run_tool_exec_command(argv[1], argc, argv, 2, false);
}

static int cmd_tool_debug(int argc, char **argv)
{
    const char *tools_json = tool_registry_get_tools_json();
    cJSON *tools = NULL;

    if ((argc < 2) || (strcmp(argv[1], "help") == 0)) {
        print_tool_debug_usage();
        return (argc < 2) ? 1 : 0;
    }

    if ((tools_json == NULL) || (tools_json[0] == '\0')) {
        printf("No tools registered.\n");
        return 1;
    }

    tools = cJSON_Parse(tools_json);
    if (tools == NULL) {
        printf("Failed to parse registered tools JSON.\n");
        return 1;
    }

    if (strcmp(argv[1], "list") == 0) {
        int count = cJSON_GetArraySize(tools);
        cJSON *tool = NULL;

        printf("Registered tools (%d):\n", count);
        cJSON_ArrayForEach(tool, tools) {
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(tool, "name"));
            const char *description = cJSON_GetStringValue(cJSON_GetObjectItem(tool, "description"));
            printf("- %s: %s\n",
                   (name != NULL) ? name : "(unknown)",
                   (description != NULL) ? description : "");
        }
        cJSON_Delete(tools);
        return 0;
    }

    if (strcmp(argv[1], "show") == 0) {
        cJSON *entry = NULL;
        cJSON *schema = NULL;
        char *schema_text = NULL;

        if (argc < 3) {
            print_tool_debug_usage();
            cJSON_Delete(tools);
            return 1;
        }

        entry = tool_debug_find_entry(tools, argv[2]);
        if (entry == NULL) {
            printf("Unknown tool: %s\n", argv[2]);
            cJSON_Delete(tools);
            return 1;
        }

        {
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(entry, "name"));
            const char *description = cJSON_GetStringValue(cJSON_GetObjectItem(entry, "description"));
            printf("name: %s\n", (name != NULL) ? name : "(unknown)");
            printf("description: %s\n", (description != NULL) ? description : "");
        }

        schema = cJSON_GetObjectItem(entry, "input_schema");
        schema_text = cJSON_Print(schema);
        printf("input_schema:\n%s\n", (schema_text != NULL) ? schema_text : "(none)");
        free(schema_text);
        cJSON_Delete(tools);
        return 0;
    }

    if (strcmp(argv[1], "exec") == 0) {
        int rc;

        if (argc < 3) {
            print_tool_debug_usage();
            cJSON_Delete(tools);
            return 1;
        }

        rc = run_tool_exec_command(argv[2], argc, argv, 3, true);
        cJSON_Delete(tools);
        return rc;
    }

    print_tool_debug_usage();
    cJSON_Delete(tools);
    return 1;
}

/* --- restart command --- */
static int cmd_restart(int argc, char **argv)
{
    printf("Restarting...\n");
    esp_restart();
    return 0;  /* unreachable */
}

void serial_cli_set_output(serial_cli_output_cb_t cb, void *ctx)
{
    s_output_cb = cb;
    s_output_ctx = ctx;
}

esp_err_t serial_cli_init(void)
{
    if (s_cli_initialized) {
        return ESP_OK;
    }

    esp_console_config_t console_config = {
        .max_cmdline_length = 256,
        .max_cmdline_args = 16,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    /* Register commands */
    esp_console_cmd_t help_cmd = {
        .command = "help",
        .help = "Show available commands",
        .func = &cmd_help,
    };
    esp_console_cmd_register(&help_cmd);

    /* set_wifi */
    wifi_set_args.ssid = arg_str1(NULL, NULL, "<ssid>", "WiFi SSID");
    wifi_set_args.password = arg_str1(NULL, NULL, "<password>", "WiFi password");
    wifi_set_args.end = arg_end(2);
    esp_console_cmd_t wifi_set_cmd = {
        .command = "set_wifi",
        .help = "Set WiFi SSID and password (e.g. set_wifi MySSID MyPass)",
        .func = &cmd_wifi_set,
        .argtable = &wifi_set_args,
    };
    esp_console_cmd_register(&wifi_set_cmd);

    /* set_wifi_static */
    wifi_static_args.ip = arg_str1(NULL, NULL, "<ip>", "Static IPv4 address");
    wifi_static_args.mask = arg_str1(NULL, NULL, "<mask>", "Subnet mask");
    wifi_static_args.gateway = arg_str1(NULL, NULL, "<gateway>", "Gateway IPv4 address");
    wifi_static_args.dns1 = arg_str0(NULL, NULL, "<dns1>", "Primary DNS (optional)");
    wifi_static_args.dns2 = arg_str0(NULL, NULL, "<dns2>", "Secondary DNS (optional)");
    wifi_static_args.end = arg_end(5);
    esp_console_cmd_t wifi_static_cmd = {
        .command = "set_wifi_static",
        .help = "Set static IP: set_wifi_static <ip> <mask> <gateway> [dns1] [dns2]",
        .func = &cmd_set_wifi_static,
        .argtable = &wifi_static_args,
    };
    esp_console_cmd_register(&wifi_static_cmd);

    /* clear_wifi_static */
    esp_console_cmd_t clear_wifi_static_cmd = {
        .command = "clear_wifi_static",
        .help = "Disable static IP and clear related config",
        .func = &cmd_clear_wifi_static,
    };
    esp_console_cmd_register(&clear_wifi_static_cmd);

    /* wifi_status */
    esp_console_cmd_t wifi_status_cmd = {
        .command = "wifi_status",
        .help = "Show WiFi connection status and static IP config",
        .func = &cmd_wifi_status,
    };
    esp_console_cmd_register(&wifi_status_cmd);

    /* wifi_scan */
    esp_console_cmd_t wifi_scan_cmd = {
        .command = "wifi_scan",
        .help = "Scan and list nearby WiFi APs",
        .func = &cmd_wifi_scan,
    };
    esp_console_cmd_register(&wifi_scan_cmd);

    /* net_test */
    esp_console_cmd_t net_test_cmd = {
        .command = "net_test",
        .help = "Test DNS/HTTPS connectivity to www.google.com and api.telegram.org",
        .func = &cmd_net_test,
    };
    esp_console_cmd_register(&net_test_cmd);

    /* set_tg_token */
    tg_token_args.token = arg_str1(NULL, NULL, "<token>", "Telegram bot token");
    tg_token_args.end = arg_end(1);
    esp_console_cmd_t tg_token_cmd = {
        .command = "set_tg_token",
        .help = "Set Telegram bot token",
        .func = &cmd_set_tg_token,
        .argtable = &tg_token_args,
    };
    esp_console_cmd_register(&tg_token_cmd);

    /* set_api_key */
    api_key_args.key = arg_str1(NULL, NULL, "<key>", "LLM API key");
    api_key_args.end = arg_end(1);
    esp_console_cmd_t api_key_cmd = {
        .command = "set_api_key",
        .help = "Set LLM API key",
        .func = &cmd_set_api_key,
        .argtable = &api_key_args,
    };
    esp_console_cmd_register(&api_key_cmd);

    /* set_model */
    model_args.model = arg_str1(NULL, NULL, "<model>", "Model identifier");
    model_args.end = arg_end(1);
    esp_console_cmd_t model_cmd = {
        .command = "set_model",
        .help = "Set LLM model (default: " MIMI_LLM_DEFAULT_MODEL ")",
        .func = &cmd_set_model,
        .argtable = &model_args,
    };
    esp_console_cmd_register(&model_cmd);

    /* set_model_provider */
    provider_args.provider = arg_str1(NULL, NULL, "<provider>", "Model provider (anthropic|openai|openai_compat|deepseek)");
    provider_args.end = arg_end(1);
    esp_console_cmd_t provider_cmd = {
        .command = "set_model_provider",
        .help = "Set LLM provider: anthropic | openai | openai_compat | deepseek",
        .func = &cmd_set_model_provider,
        .argtable = &provider_args,
    };
    esp_console_cmd_register(&provider_cmd);

    /* set_llm_base_url */
    llm_base_url_args.url = arg_str1(NULL, NULL, "<https_url>", "Custom LLM endpoint URL");
    llm_base_url_args.end = arg_end(1);
    esp_console_cmd_t llm_base_url_cmd = {
        .command = "set_llm_base_url",
        .help = "Set custom HTTPS endpoint (e.g. https://host/v1/chat/completions)",
        .func = &cmd_set_llm_base_url,
        .argtable = &llm_base_url_args,
    };
    esp_console_cmd_register(&llm_base_url_cmd);

    /* clear_llm_base_url */
    esp_console_cmd_t clear_llm_base_url_cmd = {
        .command = "clear_llm_base_url",
        .help = "Clear custom LLM endpoint and use provider default",
        .func = &cmd_clear_llm_base_url,
    };
    esp_console_cmd_register(&clear_llm_base_url_cmd);

    /* skill_list */
    esp_console_cmd_t skill_list_cmd = {
        .command = "skill_list",
        .help = "List installed skills from /spiffs/skills/",
        .func = &cmd_skill_list,
    };
    esp_console_cmd_register(&skill_list_cmd);

    /* skill_show */
    skill_show_args.name = arg_str1(NULL, NULL, "<name>", "Skill name (e.g. weather or weather.md)");
    skill_show_args.end = arg_end(1);
    esp_console_cmd_t skill_show_cmd = {
        .command = "skill_show",
        .help = "Print full content of one skill file",
        .func = &cmd_skill_show,
        .argtable = &skill_show_args,
    };
    esp_console_cmd_register(&skill_show_cmd);

    /* skill_search */
    skill_search_args.keyword = arg_str1(NULL, NULL, "<keyword>", "Keyword to search in skills");
    skill_search_args.end = arg_end(1);
    esp_console_cmd_t skill_search_cmd = {
        .command = "skill_search",
        .help = "Search skill files by keyword (filename + content)",
        .func = &cmd_skill_search,
        .argtable = &skill_search_args,
    };
    esp_console_cmd_register(&skill_search_cmd);

    /* memory_read */
    esp_console_cmd_t mem_read_cmd = {
        .command = "memory_read",
        .help = "Read MEMORY.md",
        .func = &cmd_memory_read,
    };
    esp_console_cmd_register(&mem_read_cmd);

    /* memory_write */
    memory_write_args.content = arg_str1(NULL, NULL, "<content>", "Content to write");
    memory_write_args.end = arg_end(1);
    esp_console_cmd_t mem_write_cmd = {
        .command = "memory_write",
        .help = "Write to MEMORY.md",
        .func = &cmd_memory_write,
        .argtable = &memory_write_args,
    };
    esp_console_cmd_register(&mem_write_cmd);

    /* session_list */
    esp_console_cmd_t sess_list_cmd = {
        .command = "session_list",
        .help = "List all sessions",
        .func = &cmd_session_list,
    };
    esp_console_cmd_register(&sess_list_cmd);

    /* session_clear */
    session_clear_args.chat_id = arg_str1(NULL, NULL, "<chat_id>", "Chat ID to clear");
    session_clear_args.end = arg_end(1);
    esp_console_cmd_t sess_clear_cmd = {
        .command = "session_clear",
        .help = "Clear a session",
        .func = &cmd_session_clear,
        .argtable = &session_clear_args,
    };
    esp_console_cmd_register(&sess_clear_cmd);

    /* heap_info */
    esp_console_cmd_t heap_cmd = {
        .command = "heap_info",
        .help = "Show heap memory usage",
        .func = &cmd_heap_info,
    };
    esp_console_cmd_register(&heap_cmd);

    /* sd_status */
    esp_console_cmd_t sd_status_cmd = {
        .command = "sd_status",
        .help = "Show SD card mount and filesystem status",
        .func = &cmd_sd_status,
    };
    esp_console_cmd_register(&sd_status_cmd);

    /* sd_mount */
    esp_console_cmd_t sd_mount_cmd = {
        .command = "sd_mount",
        .help = "Attempt to mount the SD card now",
        .func = &cmd_sd_mount,
    };
    esp_console_cmd_register(&sd_mount_cmd);

    /* set_search_key */
    search_key_args.key = arg_str1(NULL, NULL, "<key>", "Optional search API key (unused by local SearXNG)");
    search_key_args.end = arg_end(1);
    esp_console_cmd_t search_key_cmd = {
        .command = "set_search_key",
        .help = "Set optional search API key for web_search tool (unused by local SearXNG)",
        .func = &cmd_set_search_key,
        .argtable = &search_key_args,
    };
    esp_console_cmd_register(&search_key_cmd);

    /* set_proxy */
    proxy_args.host = arg_str1(NULL, NULL, "<host>", "Proxy host/IP");
    proxy_args.port = arg_int1(NULL, NULL, "<port>", "Proxy port");
    proxy_args.type = arg_str0(NULL, NULL, "<type>", "Proxy type: http|socks5 (default: http)");
    proxy_args.end = arg_end(3);
    esp_console_cmd_t proxy_cmd = {
        .command = "set_proxy",
        .help = "Set proxy (e.g. set_proxy 192.168.1.83 7897 [http|socks5])",
        .func = &cmd_set_proxy,
        .argtable = &proxy_args,
    };
    esp_console_cmd_register(&proxy_cmd);

    /* clear_proxy */
    esp_console_cmd_t clear_proxy_cmd = {
        .command = "clear_proxy",
        .help = "Remove proxy configuration",
        .func = &cmd_clear_proxy,
    };
    esp_console_cmd_register(&clear_proxy_cmd);

    /* config_show */
    esp_console_cmd_t config_show_cmd = {
        .command = "config_show",
        .help = "Show current configuration (build-time + NVS)",
        .func = &cmd_config_show,
    };
    esp_console_cmd_register(&config_show_cmd);

    /* config_reset */
    esp_console_cmd_t config_reset_cmd = {
        .command = "config_reset",
        .help = "Clear all NVS overrides, revert to build-time defaults",
        .func = &cmd_config_reset,
    };
    esp_console_cmd_register(&config_reset_cmd);

    /* heartbeat_trigger */
    esp_console_cmd_t heartbeat_cmd = {
        .command = "heartbeat_trigger",
        .help = "Manually trigger a heartbeat check",
        .func = &cmd_heartbeat_trigger,
    };
    esp_console_cmd_register(&heartbeat_cmd);

    /* cron_start */
    esp_console_cmd_t cron_start_cmd = {
        .command = "cron_start",
        .help = "Start cron scheduler timer now",
        .func = &cmd_cron_start,
    };
    esp_console_cmd_register(&cron_start_cmd);

    /* touch_cal */
    esp_console_cmd_t touch_cal_cmd = {
        .command = "touch_cal",
        .help = "Touch calibration UI and transform control",
        .func = &cmd_touch_cal,
    };
    esp_console_cmd_register(&touch_cal_cmd);

    /* tool_exec */
    esp_console_cmd_t tool_exec_cmd = {
        .command = "tool_exec",
        .help = "Execute a registered tool: tool_exec <name> '{...json...}'",
        .func = &cmd_tool_exec,
    };
    esp_console_cmd_register(&tool_exec_cmd);

    /* tool_debug */
    esp_console_cmd_t tool_debug_cmd = {
        .command = "tool_debug",
        .help = "Inspect or execute tools: tool_debug list | show <name> | exec <name> '{...json...}'",
        .func = &cmd_tool_debug,
    };
    esp_console_cmd_register(&tool_debug_cmd);

    /* restart */
    esp_console_cmd_t restart_cmd = {
        .command = "restart",
        .help = "Restart the device",
        .func = &cmd_restart,
    };
    esp_console_cmd_register(&restart_cmd);

    s_cli_initialized = true;
    ESP_LOGI(TAG, "CLI command core initialized");

    return ESP_OK;
}

esp_err_t serial_cli_run_line(const char *line)
{
    if (!line) {
        return ESP_ERR_INVALID_ARG;
    }

    while (*line && isspace((unsigned char)*line)) {
        line++;
    }
    if (!*line) {
        return ESP_OK;
    }

    int cmd_ret = 0;
    esp_err_t err = esp_console_run(line, &cmd_ret);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("Unknown command: %s\n", line);
        return err;
    }
    if (err == ESP_ERR_INVALID_ARG) {
        printf("Invalid command line.\n");
        return err;
    }
    if (err != ESP_OK) {
        printf("Command execution failed: %s\n", esp_err_to_name(err));
        return err;
    }

    if (cmd_ret != 0) {
        printf("Command returned %d.\n", cmd_ret);
    }
    return ESP_OK;
}
