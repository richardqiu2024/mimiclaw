#include "wifi_manager.h"
#include "mimi_config.h"

#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "wifi";

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;
static char s_ip_str[16] = "0.0.0.0";
static bool s_connected = false;
static esp_netif_t *s_sta_netif = NULL;

#define WIFI_IPV4_STR_LEN 16

typedef struct {
    char ip[WIFI_IPV4_STR_LEN];
    char netmask[WIFI_IPV4_STR_LEN];
    char gateway[WIFI_IPV4_STR_LEN];
    char dns1[WIFI_IPV4_STR_LEN];
    char dns2[WIFI_IPV4_STR_LEN];
} wifi_static_cfg_t;

static bool parse_ipv4(const char *text, esp_ip4_addr_t *out)
{
    if (!text || !text[0] || !out) {
        return false;
    }
    return esp_netif_str_to_ip4(text, out) == ESP_OK;
}

static esp_err_t nvs_read_str(nvs_handle_t nvs, const char *key,
                              char *out, size_t out_len, bool required)
{
    if (!out || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0] = '\0';
    size_t len = out_len;
    esp_err_t err = nvs_get_str(nvs, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return required ? ESP_ERR_NOT_FOUND : ESP_OK;
    }
    return err;
}

static esp_err_t wifi_static_cfg_load(wifi_static_cfg_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(cfg, 0, sizeof(*cfg));

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_WIFI, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t enabled = 0;
    err = nvs_get_u8(nvs, MIMI_NVS_KEY_WIFI_STATIC_EN, &enabled);
    if (err != ESP_OK || enabled == 0) {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    err = nvs_read_str(nvs, MIMI_NVS_KEY_WIFI_IP, cfg->ip, sizeof(cfg->ip), true);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }
    err = nvs_read_str(nvs, MIMI_NVS_KEY_WIFI_MASK, cfg->netmask, sizeof(cfg->netmask), true);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }
    err = nvs_read_str(nvs, MIMI_NVS_KEY_WIFI_GW, cfg->gateway, sizeof(cfg->gateway), true);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }
    err = nvs_read_str(nvs, MIMI_NVS_KEY_WIFI_DNS1, cfg->dns1, sizeof(cfg->dns1), false);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }
    err = nvs_read_str(nvs, MIMI_NVS_KEY_WIFI_DNS2, cfg->dns2, sizeof(cfg->dns2), false);
    nvs_close(nvs);
    return err;
}

static esp_err_t wifi_set_dns_if_present(esp_netif_dns_type_t type, const char *dns_str)
{
    if (!dns_str || !dns_str[0]) {
        return ESP_OK;
    }

    esp_ip4_addr_t dns_ip;
    if (!parse_ipv4(dns_str, &dns_ip)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_dns_info_t dns = {0};
    dns.ip.u_addr.ip4 = dns_ip;
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    return esp_netif_set_dns_info(s_sta_netif, type, &dns);
}

static esp_err_t wifi_apply_static_ip(void)
{
    if (!s_sta_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_static_cfg_t cfg = {0};
    esp_err_t err = wifi_static_cfg_load(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    esp_ip4_addr_t ip = {0};
    esp_ip4_addr_t netmask = {0};
    esp_ip4_addr_t gateway = {0};
    if (!parse_ipv4(cfg.ip, &ip) ||
        !parse_ipv4(cfg.netmask, &netmask) ||
        !parse_ipv4(cfg.gateway, &gateway)) {
        ESP_LOGE(TAG, "Static IP config invalid in NVS");
        return ESP_ERR_INVALID_ARG;
    }

    err = esp_netif_dhcpc_stop(s_sta_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "Failed stopping DHCP client: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_ip_info_t ip_info = {0};
    ip_info.ip = ip;
    ip_info.netmask = netmask;
    ip_info.gw = gateway;
    err = esp_netif_set_ip_info(s_sta_netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed setting static IP info: %s", esp_err_to_name(err));
        return err;
    }

    const char *dns1_to_apply = cfg.dns1;
    if (!dns1_to_apply[0]) {
        dns1_to_apply = cfg.gateway;
        ESP_LOGW(TAG, "Static DNS1 empty, fallback to gateway DNS: %s", dns1_to_apply);
    }

    err = wifi_set_dns_if_present(ESP_NETIF_DNS_MAIN, dns1_to_apply);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed setting DNS1: %s", esp_err_to_name(err));
        return err;
    }
    err = wifi_set_dns_if_present(ESP_NETIF_DNS_BACKUP, cfg.dns2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed setting DNS2: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Static IP applied: ip=%s mask=%s gw=%s dns1=%s dns2=%s",
             cfg.ip, cfg.netmask, cfg.gateway,
             dns1_to_apply[0] ? dns1_to_apply : "-",
             cfg.dns2[0] ? cfg.dns2 : "-");
    return ESP_OK;
}

static const char *wifi_reason_to_str(wifi_err_reason_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_MIC_FAILURE: return "MIC_FAILURE";
    case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
    default: return "UNKNOWN";
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        esp_err_t err = wifi_apply_static_ip();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Static IPv4 mode enabled");
        } else if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Static IP apply skipped: %s", esp_err_to_name(err));
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        if (disc) {
            ESP_LOGW(TAG, "Disconnected (reason=%d:%s)", disc->reason, wifi_reason_to_str(disc->reason));
        }
        if (s_retry_count < MIMI_WIFI_MAX_RETRY) {
            /* Exponential backoff: 1s, 2s, 4s, 8s, ... capped at 30s */
            uint32_t delay_ms = MIMI_WIFI_RETRY_BASE_MS << s_retry_count;
            if (delay_ms > MIMI_WIFI_RETRY_MAX_MS) {
                delay_ms = MIMI_WIFI_RETRY_MAX_MS;
            }
            ESP_LOGW(TAG, "Disconnected, retry %d/%d in %" PRIu32 "ms",
                     s_retry_count + 1, MIMI_WIFI_MAX_RETRY, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            esp_wifi_connect();
            s_retry_count++;
        } else {
            ESP_LOGE(TAG, "Failed to connect after %d retries", MIMI_WIFI_MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connected! IP: %s", s_ip_str);
        s_retry_count = 0;
        s_connected = true;

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
        ESP_LOGE(TAG, "Failed to create default STA netif");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    wifi_config_t wifi_cfg = {0};
    bool found = false;

    s_connected = false;
    s_retry_count = 0;
    snprintf(s_ip_str, sizeof(s_ip_str), "0.0.0.0");
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    /* NVS overrides take highest priority (set via CLI) */
    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_WIFI, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(wifi_cfg.sta.ssid);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_SSID, (char *)wifi_cfg.sta.ssid, &len) == ESP_OK) {
            len = sizeof(wifi_cfg.sta.password);
            nvs_get_str(nvs, MIMI_NVS_KEY_PASS, (char *)wifi_cfg.sta.password, &len);
            found = true;
        }
        nvs_close(nvs);
    }

    /* Fall back to build-time secrets */
    if (!found) {
        if (MIMI_SECRET_WIFI_SSID[0] != '\0') {
            strncpy((char *)wifi_cfg.sta.ssid, MIMI_SECRET_WIFI_SSID, sizeof(wifi_cfg.sta.ssid) - 1);
            strncpy((char *)wifi_cfg.sta.password, MIMI_SECRET_WIFI_PASS, sizeof(wifi_cfg.sta.password) - 1);
            found = true;
        }
    }

    if (!found) {
        ESP_LOGW(TAG, "No WiFi credentials. Use CLI: set_wifi <SSID> <PASS>");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s", wifi_cfg.sta.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, ticks);

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

const char *wifi_manager_get_ip(void)
{
    return s_ip_str;
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password)
{
    if (!ssid || !password || !ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_WIFI, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, MIMI_NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, MIMI_NVS_KEY_PASS, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "WiFi credentials saved for SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_set_static_ip(const char *ip,
                                     const char *netmask,
                                     const char *gateway,
                                     const char *dns1,
                                     const char *dns2)
{
    const char *effective_dns1 = (dns1 && dns1[0]) ? dns1 : gateway;

    esp_ip4_addr_t parsed = {0};
    if (!parse_ipv4(ip, &parsed) ||
        !parse_ipv4(netmask, &parsed) ||
        !parse_ipv4(gateway, &parsed)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (dns1 && dns1[0] && !parse_ipv4(dns1, &parsed)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dns2 && dns2[0] && !parse_ipv4(dns2, &parsed)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!dns1 || !dns1[0]) {
        ESP_LOGW(TAG, "Static DNS1 not provided, defaulting to gateway: %s", gateway);
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_WIFI, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(nvs, MIMI_NVS_KEY_WIFI_STATIC_EN, 1);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, MIMI_NVS_KEY_WIFI_IP, ip);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, MIMI_NVS_KEY_WIFI_MASK, netmask);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, MIMI_NVS_KEY_WIFI_GW, gateway);
    }
    if (err == ESP_OK) {
        if (effective_dns1 && effective_dns1[0]) {
            err = nvs_set_str(nvs, MIMI_NVS_KEY_WIFI_DNS1, effective_dns1);
        } else {
            err = nvs_erase_key(nvs, MIMI_NVS_KEY_WIFI_DNS1);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                err = ESP_OK;
            }
        }
    }
    if (err == ESP_OK) {
        if (dns2 && dns2[0]) {
            err = nvs_set_str(nvs, MIMI_NVS_KEY_WIFI_DNS2, dns2);
        } else {
            err = nvs_erase_key(nvs, MIMI_NVS_KEY_WIFI_DNS2);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                err = ESP_OK;
            }
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Static WiFi config saved: ip=%s mask=%s gw=%s dns1=%s dns2=%s",
             ip, netmask, gateway,
             (effective_dns1 && effective_dns1[0]) ? effective_dns1 : "-",
             (dns2 && dns2[0]) ? dns2 : "-");
    return ESP_OK;
}

esp_err_t wifi_manager_clear_static_ip(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_WIFI, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(nvs, MIMI_NVS_KEY_WIFI_STATIC_EN, 0);
    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, MIMI_NVS_KEY_WIFI_IP);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, MIMI_NVS_KEY_WIFI_MASK);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, MIMI_NVS_KEY_WIFI_GW);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, MIMI_NVS_KEY_WIFI_DNS1);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, MIMI_NVS_KEY_WIFI_DNS2);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Static WiFi config cleared; DHCP mode will be used");
    return ESP_OK;
}

esp_err_t wifi_manager_get_static_ip(char *ip, size_t ip_len,
                                     char *netmask, size_t netmask_len,
                                     char *gateway, size_t gateway_len,
                                     char *dns1, size_t dns1_len,
                                     char *dns2, size_t dns2_len)
{
    if (!ip || ip_len == 0 ||
        !netmask || netmask_len == 0 ||
        !gateway || gateway_len == 0 ||
        !dns1 || dns1_len == 0 ||
        !dns2 || dns2_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ip[0] = '\0';
    netmask[0] = '\0';
    gateway[0] = '\0';
    dns1[0] = '\0';
    dns2[0] = '\0';

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_WIFI, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t enabled = 0;
    err = nvs_get_u8(nvs, MIMI_NVS_KEY_WIFI_STATIC_EN, &enabled);
    if (err != ESP_OK || enabled == 0) {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    err = nvs_read_str(nvs, MIMI_NVS_KEY_WIFI_IP, ip, ip_len, true);
    if (err == ESP_OK) {
        err = nvs_read_str(nvs, MIMI_NVS_KEY_WIFI_MASK, netmask, netmask_len, true);
    }
    if (err == ESP_OK) {
        err = nvs_read_str(nvs, MIMI_NVS_KEY_WIFI_GW, gateway, gateway_len, true);
    }
    if (err == ESP_OK) {
        err = nvs_read_str(nvs, MIMI_NVS_KEY_WIFI_DNS1, dns1, dns1_len, false);
    }
    if (err == ESP_OK) {
        err = nvs_read_str(nvs, MIMI_NVS_KEY_WIFI_DNS2, dns2, dns2_len, false);
    }
    nvs_close(nvs);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    return err;
}

bool wifi_manager_static_ip_enabled(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMI_NVS_WIFI, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t enabled = 0;
    err = nvs_get_u8(nvs, MIMI_NVS_KEY_WIFI_STATIC_EN, &enabled);
    nvs_close(nvs);
    return (err == ESP_OK && enabled != 0);
}

EventGroupHandle_t wifi_manager_get_event_group(void)
{
    return s_wifi_event_group;
}

void wifi_manager_scan_and_print(void)
{
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    ESP_LOGI(TAG, "Scanning nearby APs...");

    /* Pause auto-connect to allow scan */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true /* block */);
    if (err == ESP_ERR_WIFI_STATE) {
        /* Try a quick stop/start cycle and scan again */
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_wifi_start();
        vTaskDelay(pdMS_TO_TICKS(200));
        err = esp_wifi_scan_start(&scan_cfg, true /* block */);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(err));
        esp_wifi_connect();
        return;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        ESP_LOGW(TAG, "No APs found");
        esp_wifi_connect();
        return;
    }

    wifi_ap_record_t *ap_list = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!ap_list) {
        ESP_LOGE(TAG, "Out of memory for AP list");
        return;
    }

    uint16_t ap_max = ap_count;
    if (esp_wifi_scan_get_ap_records(&ap_max, ap_list) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP records");
        free(ap_list);
        esp_wifi_connect();
        return;
    }

    ESP_LOGI(TAG, "Found %u APs:", ap_max);
    for (uint16_t i = 0; i < ap_max; i++) {
        const wifi_ap_record_t *ap = &ap_list[i];
        ESP_LOGI(TAG, "  [%u] SSID=%s RSSI=%d CH=%d Auth=%d",
                 i + 1, (const char *)ap->ssid, ap->rssi, ap->primary, ap->authmode);
    }

    free(ap_list);
    esp_wifi_connect();
}
