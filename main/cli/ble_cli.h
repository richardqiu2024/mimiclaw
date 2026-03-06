#pragma once

#include "esp_err.h"

/**
 * Start BLE CLI transport.
 * Command parsing/execution is delegated to serial_cli core.
 */
esp_err_t ble_cli_init(void);
