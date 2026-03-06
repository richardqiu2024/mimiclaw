#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

/**
 * Initialize WiFi subsystem (STA mode).
 */
esp_err_t wifi_manager_init(void);

/**
 * Start WiFi connection. Non-blocking, fires events.
 */
esp_err_t wifi_manager_start(void);

/**
 * Block until WiFi is connected or failed.
 * @param timeout_ms  Max time to wait (portMAX_DELAY for forever)
 * @return ESP_OK if connected, ESP_ERR_TIMEOUT otherwise
 */
esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms);

/**
 * Check if WiFi is currently connected.
 */
bool wifi_manager_is_connected(void);

/**
 * Get the current IP address string (or "0.0.0.0" if not connected).
 */
const char *wifi_manager_get_ip(void);

/**
 * Save WiFi credentials to NVS.
 */
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

/**
 * Get the event group for WiFi state (WIFI_CONNECTED_BIT / WIFI_FAIL_BIT).
 */
EventGroupHandle_t wifi_manager_get_event_group(void);

/**
 * Scan and print nearby APs.
 */
void wifi_manager_scan_and_print(void);

/**
 * Persist static IPv4 config to NVS.
 * Empty dns1/dns2 are allowed and treated as "not set".
 */
esp_err_t wifi_manager_set_static_ip(const char *ip,
                                     const char *netmask,
                                     const char *gateway,
                                     const char *dns1,
                                     const char *dns2);

/**
 * Disable static IP mode and clear related values from NVS.
 */
esp_err_t wifi_manager_clear_static_ip(void);

/**
 * Read static IPv4 config from NVS.
 * Returns ESP_OK when static IP is enabled and required fields exist.
 */
esp_err_t wifi_manager_get_static_ip(char *ip, size_t ip_len,
                                     char *netmask, size_t netmask_len,
                                     char *gateway, size_t gateway_len,
                                     char *dns1, size_t dns1_len,
                                     char *dns2, size_t dns2_len);

/**
 * Check whether static IP mode is enabled in NVS.
 */
bool wifi_manager_static_ip_enabled(void);
