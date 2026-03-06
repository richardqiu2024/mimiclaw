#include "hardware/echoear_config.h"
#include <esp_log.h>
#include <driver/i2c.h>

static const char *TAG = "echoear_config";

echoear_pcb_version_t echoear_detect_pcb_version(i2c_port_t i2c_num)
{
    esp_err_t ret;

    // Try to probe ES8311 directly (V1.0 doesn't need power enable)
    ret = i2c_master_probe(i2c_num, ECHOEAR_ES8311_ADDR, 100);
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
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));
    ESP_ERROR_CHECK(gpio_set_level(ECHOEAR_CODEC_POWER_CTRL, 1));

    vTaskDelay(pdMS_TO_TICKS(100));

    ret = i2c_master_probe(i2c_num, ECHOEAR_ES8311_ADDR, 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Detected EchoEar PCB V1.2");
        return ECHOEAR_PCB_V1_2;
    }

    ESP_LOGE(TAG, "Failed to detect EchoEar PCB version");
    return ECHOEAR_PCB_UNKNOWN;
}

esp_err_t echoear_config_init(echoear_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize I2C for detection
    i2c_config_t i2c_conf = ECHOEAR_I2C_MASTER_CONFIG();
    ESP_ERROR_CHECK(i2c_param_config(ECHOEAR_AUDIO_I2C_NUM, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(ECHOEAR_AUDIO_I2C_NUM, i2c_conf.mode, 0, 0, 0));

    // Detect PCB version
    config->pcb_version = echoear_detect_pcb_version(ECHOEAR_AUDIO_I2C_NUM);

    // Set version-specific pins
    if (config->pcb_version == ECHOEAR_PCB_V1_0) {
        config->i2s_din = ECHOEAR_I2S_DIN_V1_0;
        config->pa_pin = ECHOEAR_PA_PIN_V1_0;
        config->lcd_rst = ECHOEAR_LCD_RST_V1_0;
        config->uart1_tx = ECHOEAR_UART1_TX_V1_0;
        config->uart1_rx = ECHOEAR_UART1_RX_V1_0;
        config->touch_pad2 = ECHOEAR_TOUCH_PAD2_V1_0;
    } else if (config->pcb_version == ECHOEAR_PCB_V1_2) {
        config->i2s_din = ECHOEAR_I2S_DIN_V1_2;
        config->pa_pin = ECHOEAR_PA_PIN_V1_2;
        config->lcd_rst = ECHOEAR_LCD_RST_V1_2;
        config->uart1_tx = ECHOEAR_UART1_TX_V1_2;
        config->uart1_rx = ECHOEAR_UART1_RX_V1_2;
        config->touch_pad2 = ECHOEAR_TOUCH_PAD2_V1_2;
    } else {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "EchoEar configuration initialized:");
    ESP_LOGI(TAG, "  PCB Version: %s", config->pcb_version == ECHOEAR_PCB_V1_0 ? "V1.0" : "V1.2");
    ESP_LOGI(TAG, "  I2S DIN: GPIO_%d", config->i2s_din);
    ESP_LOGI(TAG, "  PA Pin: GPIO_%d", config->pa_pin);
    ESP_LOGI(TAG, "  LCD RST: GPIO_%d", config->lcd_rst);

    return ESP_OK;
}
