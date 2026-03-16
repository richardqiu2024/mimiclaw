#include "hardware/echoear_i2c_debug.h"

#include "hardware/echoear_config.h"
#include "display/display_panel.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_port_t echoear_i2c_debug_select_port(void)
{
    if (display_panel_touch_is_ready()) {
        return ECHOEAR_TOUCH_I2C_NUM;
    }
    return ECHOEAR_DETECT_I2C_NUM;
}

esp_err_t echoear_i2c_debug_open(echoear_i2c_debug_session_t *session)
{
    i2c_config_t config = ECHOEAR_I2C_MASTER_CONFIG();
    esp_err_t err;
    i2c_port_t port;

    if (session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    session->port = echoear_i2c_debug_select_port();
    session->installed_here = false;
    session->lvgl_locked = false;

    port = session->port;
    if ((port == ECHOEAR_TOUCH_I2C_NUM) && display_panel_lvgl_is_ready()) {
        if (!display_panel_lvgl_lock(1000)) {
            return ESP_ERR_TIMEOUT;
        }
        session->lvgl_locked = true;
    }

    if (port == ECHOEAR_TOUCH_I2C_NUM) {
        return ESP_OK;
    }

    err = i2c_param_config(port, &config);
    if (err != ESP_OK) {
        if (session->lvgl_locked) {
            (void)display_panel_lvgl_unlock();
            session->lvgl_locked = false;
        }
        return err;
    }

    err = i2c_driver_install(port, config.mode, 0, 0, 0);
    if (err == ESP_OK) {
        session->installed_here = true;
        return ESP_OK;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    if (session->lvgl_locked) {
        (void)display_panel_lvgl_unlock();
        session->lvgl_locked = false;
    }
    return err;
}

void echoear_i2c_debug_close(echoear_i2c_debug_session_t *session)
{
    if (session == NULL) {
        return;
    }
    if (session->installed_here) {
        (void)i2c_driver_delete(session->port);
        session->installed_here = false;
    }
    if (session->lvgl_locked) {
        (void)display_panel_lvgl_unlock();
        session->lvgl_locked = false;
    }
}

static esp_err_t echoear_i2c_debug_session_valid(const echoear_i2c_debug_session_t *session)
{
    if (session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((session->port != ECHOEAR_TOUCH_I2C_NUM) && (session->port != ECHOEAR_DETECT_I2C_NUM)) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t echoear_i2c_debug_probe(const echoear_i2c_debug_session_t *session,
                                  uint8_t address, uint32_t timeout_ms)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t ret;

    ret = echoear_i2c_debug_session_valid(session);
    if (ret != ESP_OK) {
        return ret;
    }

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
        ret = i2c_master_cmd_begin(session->port, cmd, pdMS_TO_TICKS(timeout_ms));
    }

    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t echoear_i2c_debug_read_reg8(const echoear_i2c_debug_session_t *session,
                                      uint8_t address, uint8_t reg,
                                      uint8_t *value, uint32_t timeout_ms)
{
    esp_err_t ret = echoear_i2c_debug_session_valid(session);

    if (ret != ESP_OK) {
        return ret;
    }
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_read_device(
        session->port, address, &reg, 1, value, 1, pdMS_TO_TICKS(timeout_ms)
    );
}

esp_err_t echoear_i2c_debug_write_reg8(const echoear_i2c_debug_session_t *session,
                                       uint8_t address, uint8_t reg,
                                       uint8_t value, uint32_t timeout_ms)
{
    uint8_t payload[2] = {reg, value};
    esp_err_t ret = echoear_i2c_debug_session_valid(session);

    if (ret != ESP_OK) {
        return ret;
    }

    return i2c_master_write_to_device(
        session->port, address, payload, sizeof(payload), pdMS_TO_TICKS(timeout_ms)
    );
}

esp_err_t echoear_i2c_debug_read_regs(const echoear_i2c_debug_session_t *session,
                                      uint8_t address, uint8_t start_reg,
                                      uint8_t *data, size_t len, uint32_t timeout_ms)
{
    esp_err_t ret = echoear_i2c_debug_session_valid(session);

    if (ret != ESP_OK) {
        return ret;
    }
    if ((data == NULL) || (len == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_read_device(
        session->port, address, &start_reg, 1, data, len, pdMS_TO_TICKS(timeout_ms)
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
