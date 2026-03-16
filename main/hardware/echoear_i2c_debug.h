#pragma once

#include "esp_err.h"
#include "driver/i2c.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_port_t port;
    bool installed_here;
    bool lvgl_locked;
} echoear_i2c_debug_session_t;

esp_err_t echoear_i2c_debug_open(echoear_i2c_debug_session_t *session);

void echoear_i2c_debug_close(echoear_i2c_debug_session_t *session);

esp_err_t echoear_i2c_debug_probe(const echoear_i2c_debug_session_t *session,
                                  uint8_t address, uint32_t timeout_ms);

esp_err_t echoear_i2c_debug_read_reg8(const echoear_i2c_debug_session_t *session,
                                      uint8_t address, uint8_t reg,
                                      uint8_t *value, uint32_t timeout_ms);

esp_err_t echoear_i2c_debug_write_reg8(const echoear_i2c_debug_session_t *session,
                                       uint8_t address, uint8_t reg,
                                       uint8_t value, uint32_t timeout_ms);

esp_err_t echoear_i2c_debug_read_regs(const echoear_i2c_debug_session_t *session,
                                      uint8_t address, uint8_t start_reg,
                                      uint8_t *data, size_t len, uint32_t timeout_ms);

esp_err_t echoear_codec_power_enable(void);

#ifdef __cplusplus
}
#endif
