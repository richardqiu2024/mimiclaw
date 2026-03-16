#include "imu/bmi270_driver.h"

#include "hardware/echoear_i2c_debug.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

// Include BMI270 config file
#include "../../managed_components/espressif__bmi270_sensor/firmware/bmi270_image.h"

static const char *TAG = "bmi270";
static uint8_t s_bmi270_address = 0;
static bool s_bmi270_initialized = false;

#define BMI270_CONFIG_FILE_SIZE 8192

static esp_err_t bmi270_write_config_file(echoear_i2c_debug_session_t *session)
{
    esp_err_t err;
    uint8_t status;

    ESP_LOGI(TAG, "Uploading config file (8KB)...");

    // Disable advanced power save
    err = echoear_i2c_debug_write_reg8(session, s_bmi270_address, BMI270_REG_PWR_CONF, 0x00, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Disable power save failed");
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    // Prepare for config load
    err = echoear_i2c_debug_write_reg8(session, s_bmi270_address, BMI270_REG_INIT_CTRL, 0x00, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Init ctrl failed");
        return err;
    }

    // Write config file in 16-byte bursts
    // Address format: 12-bit addressing (4 bits in addr[0], 8 bits in addr[1])
    for (uint16_t byte_idx = 0; byte_idx < BMI270_CONFIG_FILE_SIZE; byte_idx += 16) {
        // Calculate word address (byte_address / 2)
        uint16_t word_addr = byte_idx / 2;

        // Set address using 12-bit format: addr[0] = lower 4 bits, addr[1] = upper 8 bits
        uint8_t addr_low = (uint8_t)(word_addr & 0x0F);
        uint8_t addr_high = (uint8_t)(word_addr >> 4);

        err = echoear_i2c_debug_write_reg8(session, s_bmi270_address, BMI270_REG_INIT_ADDR_0, addr_low, 100);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Set addr0 failed at %d", byte_idx);
            return err;
        }

        err = echoear_i2c_debug_write_reg8(session, s_bmi270_address, BMI270_REG_INIT_ADDR_1, addr_high, 100);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Set addr1 failed at %d", byte_idx);
            return err;
        }

        // Write 16 bytes of data
        for (uint8_t i = 0; i < 16 && (byte_idx + i) < BMI270_CONFIG_FILE_SIZE; i++) {
            err = echoear_i2c_debug_write_reg8(session, s_bmi270_address, BMI270_REG_INIT_DATA,
                                               bmi270_config_file[byte_idx + i], 100);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Write data failed at %d", byte_idx + i);
                return err;
            }
        }
    }

    ESP_LOGI(TAG, "Config file uploaded");

    // Complete config load
    err = echoear_i2c_debug_write_reg8(session, s_bmi270_address, BMI270_REG_INIT_CTRL, 0x01, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Complete init failed");
        return err;
    }

    // Wait for initialization
    vTaskDelay(pdMS_TO_TICKS(150));

    // Check internal status - MUST READ TWICE!
    // First read triggers the chip to update the status bit
    err = echoear_i2c_debug_read_reg8(session, s_bmi270_address, BMI270_REG_INTERNAL_STATUS, &status, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read status (1st) failed");
        return err;
    }

    // Second read gets the actual status
    err = echoear_i2c_debug_read_reg8(session, s_bmi270_address, BMI270_REG_INTERNAL_STATUS, &status, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read status (2nd) failed");
        return err;
    }

    ESP_LOGI(TAG, "Internal status: 0x%02x (init_ok=%d, msg_ok=%d)",
             status, status & 0x01, (status >> 1) & 0x01);

    if ((status & 0x01) != 0x01) {
        ESP_LOGE(TAG, "Config load failed, status=0x%02x", status);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Config loaded successfully");
    return ESP_OK;
}

esp_err_t bmi270_init(void)
{
    bmi270_status_t status;
    echoear_i2c_debug_session_t session = {0};
    esp_err_t err;
    uint8_t data;

    if (s_bmi270_initialized) {
        return ESP_OK;
    }

    err = bmi270_get_status(&status);
    if (err != ESP_OK || !status.chip_id_matches) {
        ESP_LOGE(TAG, "BMI270 not found or chip ID mismatch");
        return err;
    }

    s_bmi270_address = status.address;
    ESP_LOGI(TAG, "BMI270 detected at 0x%02x", s_bmi270_address);

    err = echoear_i2c_debug_open(&session);
    if (err != ESP_OK) {
        return err;
    }

    // Soft reset
    err = echoear_i2c_debug_write_reg8(&session, s_bmi270_address, BMI270_REG_CMD, BMI270_CMD_SOFT_RESET, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Soft reset failed");
        echoear_i2c_debug_close(&session);
        return err;
    }
    echoear_i2c_debug_close(&session);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Reopen session and upload config file
    err = echoear_i2c_debug_open(&session);
    if (err != ESP_OK) {
        return err;
    }

    err = bmi270_write_config_file(&session);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Config upload failed");
        echoear_i2c_debug_close(&session);
        return err;
    }

    // Enable accelerometer only
    err = echoear_i2c_debug_write_reg8(&session, s_bmi270_address, BMI270_REG_PWR_CTRL, 0x04, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Enable accel failed");
        echoear_i2c_debug_close(&session);
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // Configure accelerometer
    err = echoear_i2c_debug_write_reg8(&session, s_bmi270_address, BMI270_REG_ACC_CONF, 0xA8, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Accel config failed");
        echoear_i2c_debug_close(&session);
        return err;
    }

    err = echoear_i2c_debug_write_reg8(&session, s_bmi270_address, BMI270_REG_ACC_RANGE, 0x01, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Accel range failed");
        echoear_i2c_debug_close(&session);
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    // Check status
    err = echoear_i2c_debug_read_reg8(&session, s_bmi270_address, BMI270_REG_STATUS, &data, 100);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Final status: 0x%02x (drdy_acc=%d)", data, (data >> 7) & 1);
    }

    echoear_i2c_debug_close(&session);
    s_bmi270_initialized = true;
    ESP_LOGI(TAG, "BMI270 initialized successfully");
    return ESP_OK;
}

esp_err_t bmi270_get_status(bmi270_status_t *status)
{
    static const uint8_t addresses[] = {BMI270_I2C_ADDR_LOW, BMI270_I2C_ADDR_HIGH};
    echoear_i2c_debug_session_t session = {0};
    esp_err_t err;

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    status->probe_err = ESP_ERR_NOT_FOUND;
    status->read_err = ESP_OK;

    err = echoear_i2c_debug_open(&session);
    if (err != ESP_OK) {
        status->probe_err = err;
        return err;
    }

    for (size_t index = 0; index < (sizeof(addresses) / sizeof(addresses[0])); ++index) {
        uint8_t address = addresses[index];

        err = echoear_i2c_debug_probe(&session, address, 100);
        if (err != ESP_OK) {
            status->probe_err = err;
            continue;
        }

        status->present = true;
        status->address = address;
        status->probe_err = ESP_OK;

        err = echoear_i2c_debug_read_reg8(&session, address, BMI270_REG_CHIP_ID, &status->chip_id, 100);
        if (err != ESP_OK) {
            status->read_err = err;
            echoear_i2c_debug_close(&session);
            return err;
        }

        status->chip_id_valid = true;
        status->chip_id_matches = (status->chip_id == BMI270_CHIP_ID);
        echoear_i2c_debug_close(&session);
        return status->chip_id_matches ? ESP_OK : ESP_FAIL;
    }

    echoear_i2c_debug_close(&session);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bmi270_read_accel(bmi270_accel_t *accel)
{
    echoear_i2c_debug_session_t session = {0};
    uint8_t data[6];
    esp_err_t err;

    if (accel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_bmi270_initialized || s_bmi270_address == 0) {
        ESP_LOGE(TAG, "BMI270 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    err = echoear_i2c_debug_open(&session);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open I2C session: %s", esp_err_to_name(err));
        return err;
    }

    err = echoear_i2c_debug_read_regs(&session, s_bmi270_address, BMI270_REG_ACC_X_LSB, data, 6, 100);
    echoear_i2c_debug_close(&session);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read accel data: %s", esp_err_to_name(err));
        return err;
    }

    int16_t raw_x = (int16_t)((data[1] << 8) | data[0]);
    int16_t raw_y = (int16_t)((data[3] << 8) | data[2]);
    int16_t raw_z = (int16_t)((data[5] << 8) | data[4]);

    // Convert to g (±4g range, 16-bit resolution)
    accel->x = raw_x / 8192.0f;
    accel->y = raw_y / 8192.0f;
    accel->z = raw_z / 8192.0f;

    return ESP_OK;
}

esp_err_t bmi270_read_gyro(bmi270_gyro_t *gyro)
{
    echoear_i2c_debug_session_t session = {0};
    uint8_t data[6];
    esp_err_t err;

    if (gyro == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_bmi270_initialized || s_bmi270_address == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    err = echoear_i2c_debug_open(&session);
    if (err != ESP_OK) {
        return err;
    }

    err = echoear_i2c_debug_read_regs(&session, s_bmi270_address, BMI270_REG_GYR_X_LSB, data, 6, 100);
    echoear_i2c_debug_close(&session);

    if (err != ESP_OK) {
        return err;
    }

    int16_t raw_x = (int16_t)((data[1] << 8) | data[0]);
    int16_t raw_y = (int16_t)((data[3] << 8) | data[2]);
    int16_t raw_z = (int16_t)((data[5] << 8) | data[4]);

    // Convert to dps (±2000dps range, 16-bit resolution)
    gyro->x = raw_x / 16.384f;
    gyro->y = raw_y / 16.384f;
    gyro->z = raw_z / 16.384f;

    return ESP_OK;
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
