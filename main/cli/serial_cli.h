#pragma once

#include <stddef.h>
#include "esp_err.h"

/**
 * CLI output callback used by transport layers (BLE, UART, etc.).
 * `data` may contain binary-safe text and is not null-terminated.
 */
typedef void (*serial_cli_output_cb_t)(const char *data, size_t len, void *ctx);

/**
 * Initialize command registry (esp_console based).
 * Does not start any transport REPL.
 */
esp_err_t serial_cli_init(void);

/**
 * Execute one CLI command line (for example: "wifi_status").
 */
esp_err_t serial_cli_run_line(const char *line);

/**
 * Set output sink for CLI command responses.
 * If no callback is set, output falls back to stdout.
 */
void serial_cli_set_output(serial_cli_output_cb_t cb, void *ctx);
