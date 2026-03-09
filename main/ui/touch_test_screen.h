#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t touch_test_screen_init(void);
bool touch_test_screen_is_active(void);

#ifdef __cplusplus
}
#endif
