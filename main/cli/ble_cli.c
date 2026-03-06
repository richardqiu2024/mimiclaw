#include "cli/ble_cli.h"
#include "cli/serial_cli.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

/* ESP-IDF 5.5 NimBLE exports this symbol but doesn't declare it in headers. */
void ble_store_config_init(void);

#define BLE_CLI_DEVICE_NAME       "MimiClaw-CLI"
#define BLE_CLI_CMD_QUEUE_LEN     8
#define BLE_CLI_LINE_MAX          256
#define BLE_CLI_TX_CHUNK          20
#define BLE_CLI_TASK_STACK        6144
#define BLE_CLI_TASK_PRIO         4
#define BLE_CLI_LINE_FLUSH_MS     250

static const char *TAG = "ble_cli";

static ble_uuid128_t s_cli_service_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0x50, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static ble_uuid128_t s_cli_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0x50, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static ble_uuid128_t s_cli_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0x50, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

typedef struct {
    char line[BLE_CLI_LINE_MAX];
} ble_cli_cmd_t;

static bool s_initialized = false;
static bool s_notify_enabled = false;
static uint8_t s_addr_type = 0;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_val_handle = 0;
static QueueHandle_t s_cmd_queue = NULL;
static SemaphoreHandle_t s_tx_mutex = NULL;
static SemaphoreHandle_t s_rx_mutex = NULL;
static TimerHandle_t s_line_flush_timer = NULL;
static char s_rx_line[BLE_CLI_LINE_MAX];
static size_t s_rx_line_len = 0;

static int ble_cli_gap_event(struct ble_gap_event *event, void *arg);
static void ble_cli_advertise(void);
static void ble_cli_flush_timer_cb(TimerHandle_t timer);
static void ble_cli_enqueue_line(void);

static void ble_cli_send_raw(const char *data, size_t len)
{
    if (!data || len == 0) return;
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_notify_enabled || s_tx_val_handle == 0) return;
    if (!s_tx_mutex) return;

    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;

    size_t off = 0;
    while (off < len) {
        if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_notify_enabled) break;

        size_t chunk = len - off;
        if (chunk > BLE_CLI_TX_CHUNK) chunk = BLE_CLI_TX_CHUNK;

        struct os_mbuf *om = ble_hs_mbuf_from_flat(data + off, chunk);
        if (!om) {
            ESP_LOGW(TAG, "No mbuf for notify chunk");
            break;
        }

        int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "Notify failed rc=%d", rc);
            break;
        }

        off += chunk;
        if (off < len) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    xSemaphoreGive(s_tx_mutex);
}

static void ble_cli_send_text(const char *text)
{
    if (!text) return;
    ble_cli_send_raw(text, strlen(text));
}

static void ble_cli_output_cb(const char *data, size_t len, void *ctx)
{
    (void)ctx;
    ble_cli_send_raw(data, len);
}

static void ble_cli_flush_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    ble_cli_enqueue_line();
}

static void ble_cli_enqueue_line_locked(void)
{
    if (s_rx_line_len == 0 || !s_cmd_queue) return;

    ble_cli_cmd_t cmd = {0};
    memcpy(cmd.line, s_rx_line, s_rx_line_len);
    cmd.line[s_rx_line_len] = '\0';
    s_rx_line_len = 0;

    if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
        ble_cli_send_text("\r\nCLI busy, command dropped.\r\nmimi> ");
    }
}

static void ble_cli_enqueue_line(void)
{
    if (!s_rx_mutex) return;
    if (xSemaphoreTake(s_rx_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    ble_cli_enqueue_line_locked();
    xSemaphoreGive(s_rx_mutex);
}

static void ble_cli_consume_input(const uint8_t *data, size_t len)
{
    if (!s_rx_mutex) return;
    if (xSemaphoreTake(s_rx_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;

    bool saw_line_end = false;
    for (size_t i = 0; i < len; i++) {
        uint8_t ch = data[i];

        if (ch == '\r' || ch == '\n') {
            saw_line_end = true;
            if (s_line_flush_timer) {
                xTimerStop(s_line_flush_timer, 0);
            }
            ble_cli_enqueue_line_locked();
            continue;
        }

        if (ch == 0x08 || ch == 0x7f) {
            if (s_rx_line_len > 0) s_rx_line_len--;
            continue;
        }

        if (!isprint((int)ch) && ch != '\t') {
            continue;
        }

        if (s_rx_line_len >= (BLE_CLI_LINE_MAX - 1)) {
            s_rx_line_len = 0;
            ble_cli_send_text("\r\nCommand too long (max 255).\r\nmimi> ");
            continue;
        }

        s_rx_line[s_rx_line_len++] = (char)ch;
    }

    /* Support apps that send command without trailing newline. */
    if (!saw_line_end && s_rx_line_len > 0 && s_line_flush_timer) {
        xTimerReset(s_line_flush_timer, 0);
    }

    xSemaphoreGive(s_rx_mutex);
}

static int ble_cli_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    size_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0) {
        return 0;
    }

    ESP_LOGI(TAG, "BLE RX %u bytes", (unsigned)len);
    if (!s_notify_enabled) {
        ESP_LOGW(TAG, "Client has not enabled TX notifications; command output won't be sent");
    }

    uint8_t *buf = malloc(len);
    if (!buf) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    if (rc == 0) {
        ble_cli_consume_input(buf, len);
    }
    free(buf);

    return (rc == 0) ? 0 : BLE_ATT_ERR_UNLIKELY;
}

static int ble_cli_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;

    /* Notify-only characteristic; direct read/write is not supported. */
    return BLE_ATT_ERR_UNLIKELY;
}

static struct ble_gatt_chr_def s_cli_chr_defs[] = {
    {
        .uuid = &s_cli_rx_uuid.u,
        .access_cb = ble_cli_rx_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &s_cli_tx_uuid.u,
        .access_cb = ble_cli_tx_access,
        .val_handle = &s_tx_val_handle,
        .flags = BLE_GATT_CHR_F_NOTIFY,
    },
    {0},
};

static struct ble_gatt_svc_def s_cli_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_cli_service_uuid.u,
        .characteristics = s_cli_chr_defs,
    },
    {0},
};

static void ble_cli_advertise(void)
{
    struct ble_hs_adv_fields adv_fields;
    struct ble_hs_adv_fields rsp_fields;
    memset(&adv_fields, 0, sizeof(adv_fields));
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    /* Primary ADV payload keeps only flags + service UUID (<=31 bytes). */
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.uuids128 = &s_cli_service_uuid;
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: rc=%d", rc);
        return;
    }

    /* Put device name in scan response to avoid ADV payload overflow. */
    const char *name = ble_svc_gap_device_name();
    rsp_fields.name = (uint8_t *)name;
    rsp_fields.name_len = strlen(name);
    rsp_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_adv_rsp_set_fields failed: rc=%d", rc);
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           ble_cli_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE advertising as '%s'", name);
}

static int ble_cli_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                s_notify_enabled = false;
                if (s_line_flush_timer) {
                    xTimerStop(s_line_flush_timer, 0);
                }
                if (s_rx_mutex && xSemaphoreTake(s_rx_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    s_rx_line_len = 0;
                    xSemaphoreGive(s_rx_mutex);
                }
                ESP_LOGI(TAG, "BLE client connected (handle=%u)", s_conn_handle);
            } else {
                ESP_LOGW(TAG, "BLE connect failed: status=%d", event->connect.status);
                ble_cli_advertise();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE client disconnected (reason=%d)",
                     event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_notify_enabled = false;
            if (s_line_flush_timer) {
                xTimerStop(s_line_flush_timer, 0);
            }
            if (s_rx_mutex && xSemaphoreTake(s_rx_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                s_rx_line_len = 0;
                xSemaphoreGive(s_rx_mutex);
            }
            ble_cli_advertise();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ble_cli_advertise();
            return 0;

        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_tx_val_handle) {
                s_notify_enabled = event->subscribe.cur_notify;
                ESP_LOGI(TAG, "BLE notify %s", s_notify_enabled ? "enabled" : "disabled");
                if (s_notify_enabled) {
                    ble_cli_send_text("MimiClaw BLE CLI ready.\r\nType 'help' for commands.\r\nmimi> ");
                }
            }
            return 0;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "BLE MTU update: conn=%u mtu=%u",
                     event->mtu.conn_handle, event->mtu.value);
            return 0;

        default:
            return 0;
    }
}

static void ble_cli_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset: reason=%d", reason);
}

static void ble_cli_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: rc=%d", rc);
        return;
    }

    ble_cli_advertise();
}

static void ble_cli_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_cli_worker_task(void *arg)
{
    (void)arg;

    ble_cli_cmd_t cmd;
    while (1) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            serial_cli_run_line(cmd.line);
            ble_cli_send_text("\r\nmimi> ");
        }
    }
}

esp_err_t ble_cli_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(serial_cli_init());

    s_cmd_queue = xQueueCreate(BLE_CLI_CMD_QUEUE_LEN, sizeof(ble_cli_cmd_t));
    if (!s_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create BLE CLI command queue");
        return ESP_ERR_NO_MEM;
    }

    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) {
        ESP_LOGE(TAG, "Failed to create BLE CLI tx mutex");
        return ESP_ERR_NO_MEM;
    }

    s_rx_mutex = xSemaphoreCreateMutex();
    if (!s_rx_mutex) {
        ESP_LOGE(TAG, "Failed to create BLE CLI rx mutex");
        return ESP_ERR_NO_MEM;
    }

    s_line_flush_timer = xTimerCreate("ble_cli_flush",
                                      pdMS_TO_TICKS(BLE_CLI_LINE_FLUSH_MS),
                                      pdFALSE, NULL, ble_cli_flush_timer_cb);
    if (!s_line_flush_timer) {
        ESP_LOGE(TAG, "Failed to create BLE CLI line flush timer");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(ble_cli_worker_task, "ble_cli_worker", BLE_CLI_TASK_STACK,
                    NULL, BLE_CLI_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create BLE CLI worker task");
        return ESP_FAIL;
    }

    serial_cli_set_output(ble_cli_output_cb, NULL);

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = ble_cli_on_reset;
    ble_hs_cfg.sync_cb = ble_cli_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_store_config_init();

    int rc = ble_svc_gap_device_name_set(BLE_CLI_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_count_cfg(s_cli_svc_defs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_cli_svc_defs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: rc=%d", rc);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(ble_cli_host_task);

    s_initialized = true;
    ESP_LOGI(TAG, "BLE CLI initialized");
    return ESP_OK;
}

#else

static const char *TAG = "ble_cli";

esp_err_t ble_cli_init(void)
{
    ESP_LOGE(TAG, "BLE CLI requires CONFIG_BT_ENABLED and CONFIG_BT_NIMBLE_ENABLED");
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
