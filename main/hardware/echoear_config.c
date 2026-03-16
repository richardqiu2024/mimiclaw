#include "hardware/echoear_config.h"
#include "hardware/echoear_i2c_debug.h"
#include <esp_log.h>
#include <driver/i2c.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "echoear_config";

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
    echoear_i2c_debug_session_t session = {0};
    esp_err_t ret;
    (void)i2c_num;

    ret = echoear_i2c_debug_open(&session);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Shared I2C open failed during PCB detect: %s", esp_err_to_name(ret));
        return ECHOEAR_PCB_UNKNOWN;
    }

    // Try to probe ES8311 directly (V1.0 doesn't need codec power enable)
    ret = echoear_i2c_debug_probe(&session, ECHOEAR_ES8311_ADDR, 100);
    if (ret == ESP_OK) {
        echoear_i2c_debug_close(&session);
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

    ret = echoear_i2c_debug_probe(&session, ECHOEAR_ES8311_ADDR, 100);
    echoear_i2c_debug_close(&session);
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

    // Detect PCB version
    echoear_pcb_version_t version = echoear_detect_pcb_version(ECHOEAR_SHARED_I2C_NUM);

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
