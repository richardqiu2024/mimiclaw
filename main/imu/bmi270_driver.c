#include "imu/bmi270_driver.h"

#include "hardware/echoear_i2c_debug.h"

#include <stdio.h>
#include <string.h>

esp_err_t bmi270_get_status(bmi270_status_t *status)
{
    static const uint8_t addresses[] = {BMI270_I2C_ADDR_LOW, BMI270_I2C_ADDR_HIGH};
    bool installed_here = false;
    esp_err_t err;

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    status->probe_err = ESP_ERR_NOT_FOUND;
    status->read_err = ESP_OK;

    err = echoear_i2c_debug_open(&installed_here);
    if (err != ESP_OK) {
        status->probe_err = err;
        return err;
    }

    for (size_t index = 0; index < (sizeof(addresses) / sizeof(addresses[0])); ++index) {
        uint8_t address = addresses[index];

        err = echoear_i2c_debug_probe(address, 100);
        if (err != ESP_OK) {
            status->probe_err = err;
            continue;
        }

        status->present = true;
        status->address = address;
        status->probe_err = ESP_OK;

        err = echoear_i2c_debug_read_reg8(address, BMI270_REG_CHIP_ID, &status->chip_id, 100);
        if (err != ESP_OK) {
            status->read_err = err;
            echoear_i2c_debug_close(installed_here);
            return err;
        }

        status->chip_id_valid = true;
        status->chip_id_matches = (status->chip_id == BMI270_CHIP_ID);
        echoear_i2c_debug_close(installed_here);
        return status->chip_id_matches ? ESP_OK : ESP_FAIL;
    }

    echoear_i2c_debug_close(installed_here);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bmi270_format_status(char *output, size_t output_size)
{
    bmi270_status_t status;
    esp_err_t err;

    if ((output == NULL) || (output_size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    err = bmi270_get_status(&status);
    if (!status.present) {
        snprintf(output, output_size,
                 "BMI270 not found on shared I2C bus; checked 0x%02x and 0x%02x.",
                 BMI270_I2C_ADDR_LOW, BMI270_I2C_ADDR_HIGH);
        return err;
    }

    if (!status.chip_id_valid) {
        snprintf(output, output_size,
                 "BMI270 responded at 0x%02x, but chip-id read failed: %s.",
                 status.address, esp_err_to_name(status.read_err));
        return err;
    }

    if (!status.chip_id_matches) {
        snprintf(output, output_size,
                 "BMI270 candidate responded at 0x%02x, chip_id=0x%02x (expected 0x%02x).",
                 status.address, status.chip_id, BMI270_CHIP_ID);
        return err;
    }

    snprintf(output, output_size, "BMI270 OK at 0x%02x, chip_id=0x%02x.",
             status.address, status.chip_id);
    return ESP_OK;
}
