#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BMI270_DRIVER_REG_CHIP_ID   0x00
#define BMI270_DRIVER_I2C_ADDR_LOW  0x68
#define BMI270_DRIVER_I2C_ADDR_HIGH 0x69

typedef struct {
    bool present;
    bool chip_id_valid;
    bool chip_id_matches;
    bool driver_initialized;
    uint8_t address;
    uint8_t chip_id;
    esp_err_t probe_err;
    esp_err_t read_err;
    esp_err_t init_err;
} bmi270_status_t;

typedef struct {
    bool accel_data_ready;
    bool gyro_data_ready;
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
} bmi270_sample_t;

esp_err_t bmi270_driver_init(void);

bool bmi270_driver_is_ready(void);

esp_err_t bmi270_read_sample(bmi270_sample_t *sample);

esp_err_t bmi270_get_status(bmi270_status_t *status);

esp_err_t bmi270_format_status(char *output, size_t output_size);
