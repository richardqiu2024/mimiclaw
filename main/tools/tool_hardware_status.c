#include "tools/tool_hardware_status.h"

#include "audio/audio_i2c_debug.h"
#include "imu/bmi270_driver.h"

static esp_err_t tool_status_noarg_execute(esp_err_t (*formatter)(char *, size_t),
                                           const char *input_json,
                                           char *output, size_t output_size)
{
    (void)input_json;

    if (formatter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return formatter(output, output_size);
}

esp_err_t tool_bmi270_status_execute(const char *input_json, char *output, size_t output_size)
{
    return tool_status_noarg_execute(bmi270_format_status, input_json, output, output_size);
}

esp_err_t tool_codec_status_execute(const char *input_json, char *output, size_t output_size)
{
    return tool_status_noarg_execute(es8311_format_status, input_json, output, output_size);
}

esp_err_t tool_mic_adc_status_execute(const char *input_json, char *output, size_t output_size)
{
    return tool_status_noarg_execute(es7210_format_status, input_json, output, output_size);
}
