#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ES8311_I2C_ADDR 0x18
#define ES7210_I2C_ADDR 0x40

typedef struct {
    bool present;
    uint8_t address;
    uint8_t reg_start;
    uint8_t regs[4];
    size_t register_count;
    esp_err_t probe_err;
    esp_err_t read_err;
} echoear_audio_i2c_status_t;

esp_err_t es8311_get_status(echoear_audio_i2c_status_t *status);
esp_err_t es8311_format_status(char *output, size_t output_size);

esp_err_t es7210_get_status(echoear_audio_i2c_status_t *status);
esp_err_t es7210_format_status(char *output, size_t output_size);
