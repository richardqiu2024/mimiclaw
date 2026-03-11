#pragma once

#include "esp_err.h"

#include <stddef.h>

esp_err_t tool_bmi270_status_execute(const char *input_json, char *output, size_t output_size);
esp_err_t tool_codec_status_execute(const char *input_json, char *output, size_t output_size);
esp_err_t tool_mic_adc_status_execute(const char *input_json, char *output, size_t output_size);
