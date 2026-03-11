#pragma once

#include "esp_err.h"
#include "driver/i2c.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

i2c_port_t echoear_i2c_debug_port(void);

esp_err_t echoear_i2c_debug_open(bool *installed_here);

void echoear_i2c_debug_close(bool installed_here);

esp_err_t echoear_i2c_debug_probe(uint8_t address, uint32_t timeout_ms);

esp_err_t echoear_i2c_debug_read_reg8(uint8_t address, uint8_t reg,
                                      uint8_t *value, uint32_t timeout_ms);

esp_err_t echoear_i2c_debug_write_reg8(uint8_t address, uint8_t reg,
                                       uint8_t value, uint32_t timeout_ms);

esp_err_t echoear_i2c_debug_read_regs(uint8_t address, uint8_t start_reg,
                                      uint8_t *data, size_t len, uint32_t timeout_ms);

esp_err_t echoear_codec_power_enable(void);

#ifdef __cplusplus
}
#endif
