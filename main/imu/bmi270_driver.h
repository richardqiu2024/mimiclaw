#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BMI270_REG_CHIP_ID   0x00
#define BMI270_REG_ERR_REG   0x02
#define BMI270_REG_STATUS    0x03
#define BMI270_REG_ACC_X_LSB 0x0C
#define BMI270_REG_GYR_X_LSB 0x12
#define BMI270_REG_ACC_CONF  0x40
#define BMI270_REG_ACC_RANGE 0x41
#define BMI270_REG_GYR_CONF  0x42
#define BMI270_REG_GYR_RANGE 0x43
#define BMI270_REG_INIT_CTRL 0x59
#define BMI270_REG_INIT_ADDR_0 0x5B
#define BMI270_REG_INIT_ADDR_1 0x5C
#define BMI270_REG_INIT_DATA 0x5E
#define BMI270_REG_INTERNAL_STATUS 0x21
#define BMI270_REG_PWR_CONF  0x7C
#define BMI270_REG_PWR_CTRL  0x7D
#define BMI270_REG_CMD       0x7E

#define BMI270_CHIP_ID       0x24
#define BMI270_I2C_ADDR_LOW  0x68
#define BMI270_I2C_ADDR_HIGH 0x69

#define BMI270_CMD_SOFT_RESET 0xB6

typedef struct {
    bool present;
    bool chip_id_valid;
    bool chip_id_matches;
    uint8_t address;
    uint8_t chip_id;
    esp_err_t probe_err;
    esp_err_t read_err;
} bmi270_status_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} bmi270_raw_data_t;

typedef struct {
    float x;
    float y;
    float z;
} bmi270_accel_t;

typedef struct {
    float x;
    float y;
    float z;
} bmi270_gyro_t;

esp_err_t bmi270_init(void);

esp_err_t bmi270_get_status(bmi270_status_t *status);

esp_err_t bmi270_format_status(char *output, size_t output_size);

esp_err_t bmi270_read_accel(bmi270_accel_t *accel);

esp_err_t bmi270_read_gyro(bmi270_gyro_t *gyro);
