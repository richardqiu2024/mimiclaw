#include "tool_web_search.h"
#include "mimi_config.h"
#include "proxy/http_proxy.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "web_search";

static char s_search_key[128] = {0};

#define SEARCH_BUF_SIZE     (16 * 1024)
#define SEARCH_RESULT_COUNT 5
#define SEARCH_API_HOST     "192.168.1.175"
#define SEARCH_API_PORT     8888
#define SEARCH_API_PATH     "/search"
#define SEARCH_API_URL      "http://" SEARCH_API_HOST ":8888" SEARCH_API_PATH

/* ── Response accumulator ─────────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} search_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    search_buf_t *sb = (search_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        size_t needed = sb->len + evt->data_len;
        if (needed < sb->cap) {
            memcpy(sb->data + sb->len, evt->data, evt->data_len);
            sb->len += evt->data_len;
            sb->data[sb->len] = '\0';
        }
    }
    return ESP_OK;
}

/* ── Init ─────────────────────────────────────────────────────── */

esp_err_t tool_web_search_init(void)
{
    if (MIMI_SECRET_SEARCH_KEY[0] != '\0') {
        strncpy(s_search_key, MIMI_SECRET_SEARCH_KEY, sizeof(s_search_key) - 1);
    }

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_SEARCH, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[128] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_API_KEY, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_search_key, tmp, sizeof(s_search_key) - 1);
        }
        nvs_close(nvs);
    }

    ESP_LOGI(TAG, "Web search initialized (SearXNG: " SEARCH_API_HOST ":%d)", SEARCH_API_PORT);
    return ESP_OK;
}

/* ── URL-encode a query string ────────────────────────────────── */

static size_t url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;

    for (; *src && pos < dst_size - 3; src++) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[pos++] = c;
        } else if (c == ' ') {
            dst[pos++] = '+';
        } else {
            dst[pos++] = '%';
            dst[pos++] = hex[c >> 4];
            dst[pos++] = hex[c & 0x0F];
        }
    }
    dst[pos] = '\0';
    return pos;
}

/* ── Format results as readable text ──────────────────────────── */

static bool append_formatted_result(char *output, size_t output_size, size_t *offset, int index,
                                    const char *title, const char *url, const char *desc)
{
    int written;

    if ((output == NULL) || (offset == NULL) || (*offset >= output_size)) {
        return false;
    }

    written = snprintf(output + *offset, output_size - *offset,
                       "%d. %s\n   %s\n   %s\n\n",
                       index,
                       (title && title[0]) ? title : "(no title)",
                       (url && url[0]) ? url : "",
                       (desc && desc[0]) ? desc : "");
    if (written <= 0) {
        return false;
    }

    *offset += (size_t)written;
    if (*offset >= output_size) {
        output[output_size - 1] = '\0';
        return false;
    }
    return true;
}

static void format_results(cJSON *root, char *output, size_t output_size)
{
    cJSON *results = cJSON_GetObjectItem(root, "results");
    cJSON *answers = cJSON_GetObjectItem(root, "answers");
    cJSON *infoboxes = cJSON_GetObjectItem(root, "infoboxes");
    size_t off = 0;
    int idx = 0;
    cJSON *item;

    output[0] = '\0';

    if (answers && cJSON_IsArray(answers)) {
        cJSON_ArrayForEach(item, answers) {
            if (idx >= SEARCH_RESULT_COUNT) break;
            if (!cJSON_IsString(item) || item->valuestring[0] == '\0') continue;

            if (!append_formatted_result(output, output_size, &off, idx + 1,
                                         "Direct answer", "", item->valuestring)) {
                break;
            }
            idx++;
        }
    }

    if (results && cJSON_IsArray(results)) {
        cJSON_ArrayForEach(item, results) {
            cJSON *title;
            cJSON *url;
            cJSON *desc;

            if (idx >= SEARCH_RESULT_COUNT) break;

            title = cJSON_GetObjectItem(item, "title");
            url = cJSON_GetObjectItem(item, "url");
            desc = cJSON_GetObjectItem(item, "content");

            if (!append_formatted_result(
                    output, output_size, &off, idx + 1,
                    (title && cJSON_IsString(title)) ? title->valuestring : NULL,
                    (url && cJSON_IsString(url)) ? url->valuestring : NULL,
                    (desc && cJSON_IsString(desc)) ? desc->valuestring : NULL)) {
                break;
            }
            idx++;
        }
    }

    if (infoboxes && cJSON_IsArray(infoboxes)) {
        cJSON_ArrayForEach(item, infoboxes) {
            cJSON *title;
            cJSON *label;
            cJSON *url;
            cJSON *id;
            cJSON *desc;
            cJSON *urls;
            cJSON *first_url;
            cJSON *nested_url;

            if (idx >= SEARCH_RESULT_COUNT) break;

            title = cJSON_GetObjectItem(item, "title");
            label = cJSON_GetObjectItem(item, "infobox");
            url = cJSON_GetObjectItem(item, "url");
            id = cJSON_GetObjectItem(item, "id");
            desc = cJSON_GetObjectItem(item, "content");
            urls = cJSON_GetObjectItem(item, "urls");
            first_url = (urls && cJSON_IsArray(urls)) ? cJSON_GetArrayItem(urls, 0) : NULL;
            nested_url = first_url ? cJSON_GetObjectItem(first_url, "url") : NULL;

            if (!append_formatted_result(
                    output, output_size, &off, idx + 1,
                    (title && cJSON_IsString(title) && title->valuestring[0]) ? title->valuestring :
                    ((label && cJSON_IsString(label)) ? label->valuestring : NULL),
                    (url && cJSON_IsString(url) && url->valuestring[0]) ? url->valuestring :
                    ((id && cJSON_IsString(id) && id->valuestring[0]) ? id->valuestring :
                    ((nested_url && cJSON_IsString(nested_url)) ? nested_url->valuestring : NULL)),
                    (desc && cJSON_IsString(desc)) ? desc->valuestring : NULL)) {
                break;
            }
            idx++;
        }
    }

    if (idx == 0) {
        snprintf(output, output_size, "No web results found.");
    }
}

/* ── Direct HTTP request ──────────────────────────────────────── */

static esp_err_t search_direct(const char *url, search_buf_t *sb)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = sb,
        .timeout_ms = 15000,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Accept", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) return err;
    if (status != 200) {
        ESP_LOGE(TAG, "Search API returned %d", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ── Execute ──────────────────────────────────────────────────── */

esp_err_t tool_web_search_execute(const char *input_json, char *output, size_t output_size)
{
    /* Parse input to get query */
    cJSON *input = cJSON_Parse(input_json);
    if (!input) {
        snprintf(output, output_size, "Error: Invalid input JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *query = cJSON_GetObjectItem(input, "query");
    if (!query || !cJSON_IsString(query) || query->valuestring[0] == '\0') {
        cJSON_Delete(input);
        snprintf(output, output_size, "Error: Missing 'query' field");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Searching: %s", query->valuestring);

    /* Build URL */
    char encoded_query[256];
    url_encode(query->valuestring, encoded_query, sizeof(encoded_query));
    cJSON_Delete(input);

    /* Allocate response buffer from PSRAM */
    search_buf_t sb = {0};
    sb.data = heap_caps_calloc(1, SEARCH_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!sb.data) {
        snprintf(output, output_size, "Error: Out of memory");
        return ESP_ERR_NO_MEM;
    }
    sb.cap = SEARCH_BUF_SIZE;

    /* Make HTTP request */
    if (http_proxy_is_enabled()) {
        ESP_LOGW(TAG, "HTTP proxy is enabled globally, but web_search uses direct HTTP to local SearXNG");
    }

    char url[512];
    snprintf(url, sizeof(url), SEARCH_API_URL "?q=%s&format=json",
             encoded_query);
    esp_err_t err = search_direct(url, &sb);

    if (err != ESP_OK) {
        free(sb.data);
        snprintf(output, output_size, "Error: Search request failed");
        return err;
    }

    /* Parse and format results */
    cJSON *root = cJSON_Parse(sb.data);
    free(sb.data);

    if (!root) {
        snprintf(output, output_size, "Error: Failed to parse search results");
        return ESP_FAIL;
    }

    format_results(root, output, output_size);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Search complete, %d bytes result", (int)strlen(output));
    return ESP_OK;
}

esp_err_t tool_web_search_set_key(const char *api_key)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_SEARCH, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_API_KEY, api_key));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    strncpy(s_search_key, api_key, sizeof(s_search_key) - 1);
    ESP_LOGI(TAG, "Search API key saved (unused by local SearXNG backend)");
    return ESP_OK;
}
