#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_PANEL_TOUCH_TRANSFORM_NONE = 0,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_X,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_Y,
    DISPLAY_PANEL_TOUCH_TRANSFORM_SWAP_XY_MIRROR_XY,
    DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_X,
    DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_Y,
    DISPLAY_PANEL_TOUCH_TRANSFORM_MIRROR_XY,
} display_panel_touch_transform_t;

esp_err_t display_panel_init(void);
esp_err_t display_panel_init_lvgl(void);
bool display_panel_lvgl_lock(int timeout_ms);
bool display_panel_lvgl_unlock(void);
bool display_panel_lvgl_is_ready(void);
esp_err_t display_panel_fill_rgb565(uint16_t color);
esp_err_t display_panel_show_boot(void);
bool display_panel_is_ready(void);
/* Returns the validated LVGL reference rotation for the EchoEar round panel. */
uint16_t display_panel_get_reference_rotation_degrees(void);
uint16_t display_panel_get_rotation_degrees(void);
esp_err_t display_panel_set_rotation_degrees(uint16_t degrees);
bool display_panel_touch_is_ready(void);
display_panel_touch_transform_t display_panel_touch_get_transform_mode(void);
esp_err_t display_panel_touch_set_transform_mode(display_panel_touch_transform_t mode);
esp_err_t display_panel_touch_read_point(uint16_t *x, uint16_t *y, uint16_t *strength, bool *pressed);
esp_err_t display_panel_touch_get_flags(bool *swap_xy, bool *mirror_x, bool *mirror_y, bool *interrupt_enabled);

#ifdef __cplusplus
}
#endif
