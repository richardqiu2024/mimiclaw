#include "hardware/echoear_i2c_debug.h"

#include "hardware/echoear_config.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

i2c_port_t echoear_i2c_debug_port(void)
{
    return ECHOEAR_DETECT_I2C_NUM;
}

esp_err_t echoear_i2c_debug_open(bool *installed_here)
{
    i2c_config_t config = ECHOEAR_I2C_MASTER_CONFIG();
    esp_err_t err;

    if (installed_here == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *installed_here = false;
    err = i2c_param_config(echoear_i2c_debug_port(), &config);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(echoear_i2c_debug_port(), config.mode, 0, 0, 0);
    if (err == ESP_OK) {
        *installed_here = true;
        return ESP_OK;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return err;
}

void echoear_i2c_debug_close(bool installed_here)
{
    if (!installed_here) {
        return;
    }
    (void)i2c_driver_delete(echoear_i2c_debug_port());
}

esp_err_t echoear_i2c_debug_probe(uint8_t address, uint32_t timeout_ms)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t ret;

    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ret = i2c_master_start(cmd);
    if (ret == ESP_OK) {
        ret = i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    }
    if (ret == ESP_OK) {
        ret = i2c_master_stop(cmd);
    }
    if (ret == ESP_OK) {
        ret = i2c_master_cmd_begin(echoear_i2c_debug_port(), cmd, pdMS_TO_TICKS(timeout_ms));
    }

    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t echoear_i2c_debug_read_reg8(uint8_t address, uint8_t reg,
                                      uint8_t *value, uint32_t timeout_ms)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_read_device(
        echoear_i2c_debug_port(), address, &reg, 1, value, 1, pdMS_TO_TICKS(timeout_ms)
    );
}

esp_err_t echoear_i2c_debug_write_reg8(uint8_t address, uint8_t reg,
                                       uint8_t value, uint32_t timeout_ms)
{
    uint8_t payload[2] = {reg, value};

    return i2c_master_write_to_device(
        echoear_i2c_debug_port(), address, payload, sizeof(payload), pdMS_TO_TICKS(timeout_ms)
    );
}

esp_err_t echoear_i2c_debug_read_regs(uint8_t address, uint8_t start_reg,
                                      uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if ((data == NULL) || (len == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_read_device(
        echoear_i2c_debug_port(), address, &start_reg, 1, data, len, pdMS_TO_TICKS(timeout_ms)
    );
}

esp_err_t echoear_codec_power_enable(void)
{
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << ECHOEAR_CODEC_POWER_CTRL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&gpio_conf);

    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(ECHOEAR_CODEC_POWER_CTRL, 1);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}
