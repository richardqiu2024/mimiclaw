#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BMI270_REG_CHIP_ID   0x00
#define BMI270_CHIP_ID       0x24
#define BMI270_I2C_ADDR_LOW  0x68
#define BMI270_I2C_ADDR_HIGH 0x69

typedef struct {
    bool present;
    bool chip_id_valid;
    bool chip_id_matches;
    uint8_t address;
    uint8_t chip_id;
    esp_err_t probe_err;
    esp_err_t read_err;
} bmi270_status_t;

esp_err_t bmi270_get_status(bmi270_status_t *status);

esp_err_t bmi270_format_status(char *output, size_t output_size);
