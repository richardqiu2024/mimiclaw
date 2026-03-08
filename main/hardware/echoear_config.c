#include "hardware/echoear_config.h"
#include <esp_log.h>
#include <driver/i2c.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "echoear_config";

static esp_err_t echoear_i2c_probe(i2c_port_t i2c_num, uint8_t dev_addr, uint32_t timeout_ms)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = i2c_master_start(cmd);
    if (ret == ESP_OK) {
        ret = i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    }
    if (ret == ESP_OK) {
        ret = i2c_master_stop(cmd);
    }
    if (ret == ESP_OK) {
        ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(timeout_ms));
    }

    i2c_cmd_link_delete(cmd);
    return ret;
}

static void echoear_fill_pins_for_version(echoear_config_t *config, echoear_pcb_version_t version)
{
    config->pcb_version = version;
    if (version == ECHOEAR_PCB_V1_2) {
        config->i2s_din = ECHOEAR_I2S_DIN_V1_2;
        config->pa_pin = ECHOEAR_PA_PIN_V1_2;
        config->lcd_rst = ECHOEAR_LCD_RST_V1_2;
        config->uart1_tx = ECHOEAR_UART1_TX_V1_2;
        config->uart1_rx = ECHOEAR_UART1_RX_V1_2;
        config->touch_pad2 = ECHOEAR_TOUCH_PAD2_V1_2;
    } else {
        config->i2s_din = ECHOEAR_I2S_DIN_V1_0;
        config->pa_pin = ECHOEAR_PA_PIN_V1_0;
        config->lcd_rst = ECHOEAR_LCD_RST_V1_0;
        config->uart1_tx = ECHOEAR_UART1_TX_V1_0;
        config->uart1_rx = ECHOEAR_UART1_RX_V1_0;
        config->touch_pad2 = ECHOEAR_TOUCH_PAD2_V1_0;
    }
}

echoear_pcb_version_t echoear_detect_pcb_version(i2c_port_t i2c_num)
{
    esp_err_t ret;

    // Try to probe ES8311 directly (V1.0 doesn't need codec power enable)
    ret = echoear_i2c_probe(i2c_num, ECHOEAR_ES8311_ADDR, 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Detected EchoEar PCB V1.0");
        return ECHOEAR_PCB_V1_0;
    }

    // V1.2 requires enabling codec power first
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << ECHOEAR_CODEC_POWER_CTRL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&gpio_conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Codec power pin config failed: %s", esp_err_to_name(ret));
        return ECHOEAR_PCB_UNKNOWN;
    }
    ret = gpio_set_level(ECHOEAR_CODEC_POWER_CTRL, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Codec power set-level failed: %s", esp_err_to_name(ret));
        return ECHOEAR_PCB_UNKNOWN;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    ret = echoear_i2c_probe(i2c_num, ECHOEAR_ES8311_ADDR, 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Detected EchoEar PCB V1.2");
        return ECHOEAR_PCB_V1_2;
    }

    ESP_LOGW(TAG, "Failed to detect EchoEar PCB version");
    return ECHOEAR_PCB_UNKNOWN;
}

esp_err_t echoear_config_init(echoear_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set a deterministic fallback first.
    echoear_fill_pins_for_version(config, ECHOEAR_PCB_V1_0);

    // Initialize temporary I2C bus for board-version detection.
    esp_err_t ret;
    bool i2c_installed_here = false;
    i2c_config_t i2c_conf = ECHOEAR_I2C_MASTER_CONFIG();
    ret = i2c_param_config(ECHOEAR_DETECT_I2C_NUM, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C param config for detection failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = i2c_driver_install(ECHOEAR_DETECT_I2C_NUM, i2c_conf.mode, 0, 0, 0);
    if (ret == ESP_OK) {
        i2c_installed_here = true;
    } else if (ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "I2C driver install for detection failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Detect PCB version
    echoear_pcb_version_t version = echoear_detect_pcb_version(ECHOEAR_DETECT_I2C_NUM);

    if (i2c_installed_here) {
        esp_err_t del_ret = i2c_driver_delete(ECHOEAR_DETECT_I2C_NUM);
        if (del_ret != ESP_OK) {
            ESP_LOGW(TAG, "I2C driver delete after detection failed: %s", esp_err_to_name(del_ret));
        }
    }

    if (version == ECHOEAR_PCB_UNKNOWN) {
        ESP_LOGW(TAG, "PCB detection failed, fallback to V1.0 pin map");
        echoear_fill_pins_for_version(config, ECHOEAR_PCB_V1_0);
        return ESP_ERR_NOT_FOUND;
    }

    echoear_fill_pins_for_version(config, version);

    ESP_LOGI(TAG, "EchoEar configuration initialized:");
    ESP_LOGI(TAG, "  PCB Version: %s", config->pcb_version == ECHOEAR_PCB_V1_0 ? "V1.0" : "V1.2");
    ESP_LOGI(TAG, "  I2S DIN: GPIO_%d", config->i2s_din);
    ESP_LOGI(TAG, "  PA Pin: GPIO_%d", config->pa_pin);
    ESP_LOGI(TAG, "  LCD RST: GPIO_%d", config->lcd_rst);

    return ESP_OK;
}
