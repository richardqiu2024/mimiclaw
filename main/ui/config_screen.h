#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t config_screen_init(void);
void config_screen_set_phase(const char *phase);
void config_screen_set_ble_ready(bool ready);
void config_screen_set_agent_ready(bool ready);
void config_screen_toggle(void);
bool config_screen_is_active(void);
void config_screen_scroll_down(void);
