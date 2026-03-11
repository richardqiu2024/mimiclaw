#include "audio/audio_i2c_debug.h"

#include "hardware/echoear_i2c_debug.h"

#include <stdio.h>
#include <string.h>

static esp_err_t echoear_audio_device_get_status(uint8_t address,
                                                 echoear_audio_i2c_status_t *status)
{
    bool installed_here = false;
    esp_err_t err;

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    status->address = address;
    status->probe_err = ESP_ERR_NOT_FOUND;
    status->read_err = ESP_OK;
    status->reg_start = 0x00;

    err = echoear_codec_power_enable();
    if (err != ESP_OK) {
        status->probe_err = err;
        return err;
    }

    err = echoear_i2c_debug_open(&installed_here);
    if (err != ESP_OK) {
        status->probe_err = err;
        return err;
    }

    err = echoear_i2c_debug_probe(address, 100);
    if (err != ESP_OK) {
        status->probe_err = err;
        echoear_i2c_debug_close(installed_here);
        return err;
    }

    status->present = true;
    status->probe_err = ESP_OK;

    for (size_t index = 0; index < sizeof(status->regs); ++index) {
        err = echoear_i2c_debug_read_reg8(address, (uint8_t)(status->reg_start + index),
                                          &status->regs[index], 100);
        if (err != ESP_OK) {
            status->read_err = err;
            echoear_i2c_debug_close(installed_here);
            return err;
        }
        status->register_count++;
    }

    echoear_i2c_debug_close(installed_here);
    return ESP_OK;
}

static esp_err_t echoear_audio_device_format_status(uint8_t address, const char *label,
                                                    char *output, size_t output_size)
{
    echoear_audio_i2c_status_t status;
    esp_err_t err;

    if ((output == NULL) || (output_size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    err = echoear_audio_device_get_status(address, &status);
    if (!status.present) {
        snprintf(output, output_size, "%s not found at 0x%02x.", label, address);
        return err;
    }

    if (status.register_count < sizeof(status.regs)) {
        snprintf(output, output_size,
                 "%s ACK at 0x%02x, but register snapshot is incomplete (%u/%u): %s.",
                 label, address, (unsigned)status.register_count,
                 (unsigned)sizeof(status.regs), esp_err_to_name(status.read_err));
        return err;
    }

    snprintf(output, output_size,
             "%s OK at 0x%02x; reg00=0x%02x reg01=0x%02x reg02=0x%02x reg03=0x%02x.",
             label, address, status.regs[0], status.regs[1], status.regs[2], status.regs[3]);
    return ESP_OK;
}

esp_err_t es8311_get_status(echoear_audio_i2c_status_t *status)
{
    return echoear_audio_device_get_status(ES8311_I2C_ADDR, status);
}

esp_err_t es8311_format_status(char *output, size_t output_size)
{
    return echoear_audio_device_format_status(ES8311_I2C_ADDR, "ES8311 codec", output, output_size);
}

esp_err_t es7210_get_status(echoear_audio_i2c_status_t *status)
{
    return echoear_audio_device_get_status(ES7210_I2C_ADDR, status);
}

esp_err_t es7210_format_status(char *output, size_t output_size)
{
    return echoear_audio_device_format_status(ES7210_I2C_ADDR, "ES7210 mic ADC", output, output_size);
}
