#include "I2C_Driver.h"
#include "display/display_panel.h"


#define I2C_TRANS_BUF_MINIMUM_SIZE     (sizeof(i2c_cmd_desc_t) + \
                                        sizeof(i2c_cmd_link_t) * 8) /* It is required to have allocate one i2c_cmd_desc_t per command:
                                                                     * start + write (device address) + write buffer +
                                                                     * start + write (device address) + read buffer + read buffer for NACK +
                                                                     * stop */
static const char *I2C_TAG = "I2C";
static bool s_i2c_ready = false;
/**
 * @brief i2c master initialization
 */
static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;
    i2c_config_t conf = ECHOEAR_I2C_MASTER_CONFIG();

    esp_err_t err = i2c_param_config(i2c_master_port, &conf);
    if (err != ESP_OK) {
        return err;
    }

    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}
esp_err_t I2C_Init(void)
{
    if (s_i2c_ready) {
        return ESP_OK;
    }

    if (display_panel_touch_is_ready()) {
        s_i2c_ready = true;
        ESP_LOGI(I2C_TAG, "Reusing touch shared I2C bus on I2C%d", I2C_MASTER_NUM);
        return ESP_OK;
    }

    esp_err_t err = i2c_master_init();
    if (err == ESP_ERR_INVALID_STATE) {
        s_i2c_ready = true;
        ESP_LOGI(I2C_TAG, "Reusing shared I2C bus on I2C%d", I2C_MASTER_NUM);
        return ESP_OK;
    }
    if ((err == ESP_FAIL) && display_panel_is_ready()) {
        s_i2c_ready = true;
        ESP_LOGI(I2C_TAG, "Reusing already-installed shared I2C bus on I2C%d", I2C_MASTER_NUM);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(I2C_TAG, "I2C init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_i2c_ready = true;
    ESP_LOGI(I2C_TAG, "I2C initialized successfully");
    return ESP_OK;
}


// Reg addr is 8 bit
esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
    esp_err_t err = I2C_Init();
    if (err != ESP_OK) {
        return err;
    }
    if ((Length > 0) && (Reg_data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (Length == 0) {
        return i2c_master_write_to_device(I2C_MASTER_NUM, Driver_addr, &Reg_addr, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    }

    uint8_t buf[Length+1];
    
    buf[0] = Reg_addr;
    // Copy Reg_data to buf starting at buf[1]
    memcpy(&buf[1], Reg_data, Length);
    return i2c_master_write_to_device(I2C_MASTER_NUM, Driver_addr, buf, Length+1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}



esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
    esp_err_t err = I2C_Init();
    if (err != ESP_OK) {
        return err;
    }
    if ((Length > 0) && (Reg_data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (Length == 0) {
        return ESP_OK;
    }

    return i2c_master_write_read_device(I2C_MASTER_NUM, Driver_addr, &Reg_addr, 1, Reg_data, Length, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}
