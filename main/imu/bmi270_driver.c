#include "imu/bmi270_driver.h"

#include "imu/I2C_Driver.h"
#include "imu/bmi270_sensor_api/bmi2.h"
#include "imu/bmi270_sensor_api/bmi270.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "bmi270";

static SemaphoreHandle_t s_driver_mutex = NULL;
static struct bmi2_dev s_bmi_dev;
static bool s_driver_initialized = false;
static uint8_t s_bmi_address = BMI270_DRIVER_I2C_ADDR_LOW;
static esp_err_t s_last_init_err = ESP_ERR_INVALID_STATE;

static esp_err_t ensure_driver_mutex(void)
{
    if (s_driver_mutex != NULL) {
        return ESP_OK;
    }

    s_driver_mutex = xSemaphoreCreateRecursiveMutex();
    return (s_driver_mutex != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool lock_driver(TickType_t timeout_ticks)
{
    return (s_driver_mutex != NULL) &&
           (xSemaphoreTakeRecursive(s_driver_mutex, timeout_ticks) == pdTRUE);
}

static void unlock_driver(void)
{
    if (s_driver_mutex != NULL) {
        xSemaphoreGiveRecursive(s_driver_mutex);
    }
}

static bool bmi2_result_failed(int8_t rslt)
{
    return rslt < BMI2_OK;
}

static esp_err_t bmi2_result_to_esp_err(int8_t rslt)
{
    switch (rslt) {
    case BMI2_OK:
        return ESP_OK;
    case BMI2_E_NULL_PTR:
    case BMI2_E_INVALID_INPUT:
        return ESP_ERR_INVALID_ARG;
    case BMI2_E_DEV_NOT_FOUND:
        return ESP_ERR_NOT_FOUND;
    default:
        return ESP_FAIL;
    }
}

static esp_err_t probe_bmi270_address(uint8_t *address, uint8_t *chip_id,
                                      esp_err_t *probe_err, esp_err_t *read_err)
{
    static const uint8_t addresses[] = {
        BMI270_DRIVER_I2C_ADDR_LOW,
        BMI270_DRIVER_I2C_ADDR_HIGH,
    };
    esp_err_t init_err = I2C_Init();

    if (probe_err != NULL) {
        *probe_err = init_err;
    }
    if (read_err != NULL) {
        *read_err = ESP_OK;
    }
    if (init_err != ESP_OK) {
        return init_err;
    }

    for (size_t index = 0; index < (sizeof(addresses) / sizeof(addresses[0])); ++index) {
        uint8_t current_chip_id = 0;
        esp_err_t err = I2C_Read(addresses[index], BMI270_DRIVER_REG_CHIP_ID, &current_chip_id, 1);
        if (err != ESP_OK) {
            if (probe_err != NULL) {
                *probe_err = err;
            }
            if (read_err != NULL) {
                *read_err = err;
            }
            continue;
        }

        if (address != NULL) {
            *address = addresses[index];
        }
        if (chip_id != NULL) {
            *chip_id = current_chip_id;
        }
        if (probe_err != NULL) {
            *probe_err = ESP_OK;
        }
        if (read_err != NULL) {
            *read_err = ESP_OK;
        }

        return (current_chip_id == BMI270_CHIP_ID) ? ESP_OK : ESP_FAIL;
    }

    return ESP_ERR_NOT_FOUND;
}

static BMI2_INTF_RETURN_TYPE bmi270_i2c_read(uint8_t reg_addr, uint8_t *reg_data,
                                             uint32_t len, void *intf_ptr)
{
    uint8_t address = s_bmi_address;
    esp_err_t err;

    if (((reg_data == NULL) && (len > 0)) || (intf_ptr == NULL)) {
        return BMI2_E_NULL_PTR;
    }

    address = *(uint8_t *)intf_ptr;
    if (!lock_driver(portMAX_DELAY)) {
        return BMI2_E_COM_FAIL;
    }

    err = I2C_Read(address, reg_addr, reg_data, len);
    unlock_driver();

    return (err == ESP_OK) ? BMI2_OK : BMI2_E_COM_FAIL;
}

static BMI2_INTF_RETURN_TYPE bmi270_i2c_write(uint8_t reg_addr, const uint8_t *reg_data,
                                              uint32_t len, void *intf_ptr)
{
    uint8_t address = s_bmi_address;
    esp_err_t err;

    if (((reg_data == NULL) && (len > 0)) || (intf_ptr == NULL) || (len > BMI2_MAX_LEN)) {
        return BMI2_E_INVALID_INPUT;
    }

    address = *(uint8_t *)intf_ptr;

    if (!lock_driver(portMAX_DELAY)) {
        return BMI2_E_COM_FAIL;
    }

    err = I2C_Write(address, reg_addr, reg_data, len);
    unlock_driver();

    return (err == ESP_OK) ? BMI2_OK : BMI2_E_COM_FAIL;
}

static void bmi270_delay_us(uint32_t period, void *intf_ptr)
{
    (void)intf_ptr;
    ets_delay_us(period);
}

static int8_t bmi270_configure_accel(struct bmi2_dev *device)
{
    struct bmi2_sens_config config = {
        .type = BMI2_ACCEL,
    };
    int8_t rslt = bmi2_get_sensor_config(&config, 1, device);

    if (bmi2_result_failed(rslt)) {
        return rslt;
    }

    config.cfg.acc.odr = BMI2_ACC_ODR_200HZ;
    config.cfg.acc.range = BMI2_ACC_RANGE_2G;
    config.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    config.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;

    return bmi2_set_sensor_config(&config, 1, device);
}

static int8_t bmi270_configure_gyro(struct bmi2_dev *device)
{
    struct bmi2_sens_config config = {
        .type = BMI2_GYRO,
    };
    int8_t rslt = bmi2_get_sensor_config(&config, 1, device);

    if (bmi2_result_failed(rslt)) {
        return rslt;
    }

    config.cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
    config.cfg.gyr.range = BMI2_GYR_RANGE_2000;
    config.cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
    config.cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE;
    config.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;

    return bmi2_set_sensor_config(&config, 1, device);
}

static float lsb_to_g(int16_t value, float range_g, uint8_t bit_width)
{
    const float half_scale = (float)(1U << (bit_width - 1));
    return (range_g / half_scale) * (float)value;
}

static float lsb_to_dps(int16_t value, float range_dps, uint8_t bit_width)
{
    const float half_scale = (float)(1U << (bit_width - 1));
    return (range_dps / half_scale) * (float)value;
}

esp_err_t bmi270_driver_init(void)
{
    esp_err_t err = ensure_driver_mutex();
    int8_t rslt;

    if (err != ESP_OK) {
        s_last_init_err = err;
        return err;
    }

    if (!lock_driver(portMAX_DELAY)) {
        s_last_init_err = ESP_ERR_TIMEOUT;
        return ESP_ERR_TIMEOUT;
    }

    if (s_driver_initialized) {
        unlock_driver();
        return ESP_OK;
    }

    err = probe_bmi270_address(&s_bmi_address, NULL, NULL, NULL);
    if (err != ESP_OK) {
        s_last_init_err = err;
        unlock_driver();
        return err;
    }

    memset(&s_bmi_dev, 0, sizeof(s_bmi_dev));
    s_bmi_dev.intf = BMI2_I2C_INTF;
    s_bmi_dev.read = bmi270_i2c_read;
    s_bmi_dev.write = bmi270_i2c_write;
    s_bmi_dev.delay_us = bmi270_delay_us;
    s_bmi_dev.intf_ptr = &s_bmi_address;
    s_bmi_dev.read_write_len = BMI2_MAX_LEN;

    rslt = bmi270_init(&s_bmi_dev);
    if (bmi2_result_failed(rslt)) {
        s_last_init_err = bmi2_result_to_esp_err(rslt);
        unlock_driver();
        ESP_LOGE(TAG, "BMI270 init failed: %d", rslt);
        return s_last_init_err;
    }

    rslt = bmi270_configure_accel(&s_bmi_dev);
    if (bmi2_result_failed(rslt)) {
        s_last_init_err = bmi2_result_to_esp_err(rslt);
        unlock_driver();
        ESP_LOGE(TAG, "BMI270 accel config failed: %d", rslt);
        return s_last_init_err;
    }

    rslt = bmi270_configure_gyro(&s_bmi_dev);
    if (bmi2_result_failed(rslt)) {
        s_last_init_err = bmi2_result_to_esp_err(rslt);
        unlock_driver();
        ESP_LOGE(TAG, "BMI270 gyro config failed: %d", rslt);
        return s_last_init_err;
    }

    {
        uint8_t sensor_list[] = {BMI2_ACCEL, BMI2_GYRO};

        rslt = bmi2_sensor_enable(sensor_list, (uint8_t)(sizeof(sensor_list) / sizeof(sensor_list[0])), &s_bmi_dev);
        if (bmi2_result_failed(rslt)) {
            s_last_init_err = bmi2_result_to_esp_err(rslt);
            unlock_driver();
            ESP_LOGE(TAG, "BMI270 sensor enable failed: %d", rslt);
            return s_last_init_err;
        }
    }

    s_driver_initialized = true;
    s_last_init_err = ESP_OK;
    unlock_driver();

    ESP_LOGI(TAG, "BMI270 ready on shared touch I2C bus at 0x%02x", s_bmi_address);
    return ESP_OK;
}

bool bmi270_driver_is_ready(void)
{
    return s_driver_initialized;
}

esp_err_t bmi270_read_sample(bmi270_sample_t *sample)
{
    struct bmi2_sens_data sensor_data;
    int8_t rslt;
    esp_err_t err;

    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));

    err = bmi270_driver_init();
    if (err != ESP_OK) {
        return err;
    }

    if (!lock_driver(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }

    memset(&sensor_data, 0, sizeof(sensor_data));
    rslt = bmi2_get_sensor_data(&sensor_data, &s_bmi_dev);
    unlock_driver();

    if (bmi2_result_failed(rslt)) {
        return bmi2_result_to_esp_err(rslt);
    }

    sample->accel_data_ready = ((sensor_data.status & BMI2_DRDY_ACC) != 0);
    sample->gyro_data_ready = ((sensor_data.status & BMI2_DRDY_GYR) != 0);
    sample->accel_x_g = lsb_to_g(sensor_data.acc.x, 2.0f, s_bmi_dev.resolution);
    sample->accel_y_g = lsb_to_g(sensor_data.acc.y, 2.0f, s_bmi_dev.resolution);
    sample->accel_z_g = lsb_to_g(sensor_data.acc.z, 2.0f, s_bmi_dev.resolution);
    sample->gyro_x_dps = lsb_to_dps(sensor_data.gyr.x, 2000.0f, s_bmi_dev.resolution);
    sample->gyro_y_dps = lsb_to_dps(sensor_data.gyr.y, 2000.0f, s_bmi_dev.resolution);
    sample->gyro_z_dps = lsb_to_dps(sensor_data.gyr.z, 2000.0f, s_bmi_dev.resolution);

    return ESP_OK;
}

esp_err_t bmi270_get_status(bmi270_status_t *status)
{
    esp_err_t mutex_err = ensure_driver_mutex();
    esp_err_t probe_result;

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    status->probe_err = ESP_ERR_NOT_FOUND;
    status->read_err = ESP_OK;
    status->init_err = s_last_init_err;
    status->driver_initialized = s_driver_initialized;

    if (mutex_err != ESP_OK) {
        status->probe_err = mutex_err;
        status->read_err = mutex_err;
        status->init_err = mutex_err;
        return mutex_err;
    }

    if (!lock_driver(portMAX_DELAY)) {
        status->probe_err = ESP_ERR_TIMEOUT;
        status->read_err = ESP_ERR_TIMEOUT;
        return ESP_ERR_TIMEOUT;
    }

    probe_result = probe_bmi270_address(&status->address, &status->chip_id,
                                        &status->probe_err, &status->read_err);
    status->present = (status->probe_err == ESP_OK);
    status->chip_id_valid = status->present;
    status->chip_id_matches = status->chip_id_valid && (status->chip_id == BMI270_CHIP_ID);
    status->driver_initialized = s_driver_initialized;
    status->init_err = s_last_init_err;

    unlock_driver();
    return probe_result;
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
                 "BMI270 not found on shared touch I2C bus; checked 0x%02x and 0x%02x.",
                 BMI270_DRIVER_I2C_ADDR_LOW, BMI270_DRIVER_I2C_ADDR_HIGH);
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

    if (status.driver_initialized) {
        snprintf(output, output_size,
                 "BMI270 OK on shared touch I2C bus at 0x%02x, chip_id=0x%02x, driver ready.",
                 status.address, status.chip_id);
        return ESP_OK;
    }

    if (status.init_err == ESP_OK) {
        snprintf(output, output_size,
                 "BMI270 detected on shared touch I2C bus at 0x%02x, chip_id=0x%02x.",
                 status.address, status.chip_id);
    } else if (status.init_err == ESP_ERR_INVALID_STATE) {
        snprintf(output, output_size,
                 "BMI270 detected on shared touch I2C bus at 0x%02x, chip_id=0x%02x, driver not started yet.",
                 status.address, status.chip_id);
    } else {
        snprintf(output, output_size,
                 "BMI270 detected on shared touch I2C bus at 0x%02x, chip_id=0x%02x, last init failed: %s.",
                 status.address, status.chip_id, esp_err_to_name(status.init_err));
    }

    return err;
}
