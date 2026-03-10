#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t touch_calibration_screen_show(void);
bool touch_calibration_screen_is_active(void);
