#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * Attempt to mount the SD card at /sdcard.
 * Uses the board's 3-wire SD pins in 1-bit SDMMC mode on ESP32-S3.
 */
esp_err_t tool_sdcard_mount(void);

/**
 * Check whether /sdcard is currently mounted and accessible.
 */
bool tool_sdcard_is_mounted(void);

/**
 * Describe current SD card mount status into a caller-provided buffer.
 */
esp_err_t tool_sdcard_get_status(char *output, size_t output_size);

/**
 * Read a file from the SD card. The card is auto-mounted on demand.
 * Input JSON: {"path": "/sdcard/...", "offset": 0, "max_bytes": 4096}
 * `offset` and `max_bytes` are optional.
 */
esp_err_t tool_read_sd_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * List one directory from the SD card. The card is auto-mounted on demand.
 * Input JSON: {"path": "/sdcard/..."} (`path` is optional, default: /sdcard)
 */
esp_err_t tool_list_sd_dir_execute(const char *input_json, char *output, size_t output_size);
