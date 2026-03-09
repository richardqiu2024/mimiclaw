#include "display/display_panel.h"

#include <new>

#include "esp_display_panel.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardware/echoear_config.h"
#include "display/miniclaw_logo.h"
#include "display/lvgl_v8_port.h"

using namespace esp_panel::board;
using namespace esp_panel::drivers;

static const char *TAG = "display_panel";

static Board *s_board = nullptr;
static LCD *s_lcd = nullptr;
static uint16_t *s_fill_buffer = nullptr;
static int s_fill_buffer_lines = 0;
static echoear_config_t s_echoear_config = {
    .pcb_version = ECHOEAR_PCB_V1_0,
    .i2s_din = ECHOEAR_I2S_DIN_V1_0,
    .pa_pin = ECHOEAR_PA_PIN_V1_0,
    .lcd_rst = ECHOEAR_LCD_RST_V1_0,
    .uart1_tx = ECHOEAR_UART1_TX_V1_0,
    .uart1_rx = ECHOEAR_UART1_RX_V1_0,
    .touch_pad2 = ECHOEAR_TOUCH_PAD2_V1_0,
};
static bool s_ready = false;
static bool s_lvgl_ready = false;

static lv_disp_rot_t get_reference_rotation(void)
{
    // Validated upright display-space baseline for the EchoEar 360x360 round panel.
    // Future LVGL screens should inherit this instead of assuming LV_DISP_ROT_NONE.
    return LV_DISP_ROT_270;
}

static void configure_echoear_display_power(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << ECHOEAR_POWER_CTRL) | (1ULL << ECHOEAR_LCD_BACKLIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "gpio_config(power/backlight) failed: %s", esp_err_to_name(err));
        return;
    }

    (void)gpio_set_level(ECHOEAR_POWER_CTRL, ECHOEAR_POWER_CTRL_ON_LEVEL);
    int backlight_on_level = ECHOEAR_LCD_BACKLIGHT_INVERT ? 0 : 1;
    (void)gpio_set_level(ECHOEAR_LCD_BACKLIGHT, backlight_on_level);
    ESP_LOGI(
        TAG, "Display power configured: POWER_CTRL(GPIO%d)=%d",
        (int)ECHOEAR_POWER_CTRL, ECHOEAR_POWER_CTRL_ON_LEVEL
    );
    ESP_LOGI(
        TAG, "Backlight GPIO forced on: GPIO%d=%d", (int)ECHOEAR_LCD_BACKLIGHT, backlight_on_level
    );
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void load_echoear_display_config(void)
{
    echoear_config_t cfg = {};
    esp_err_t err = echoear_config_init(&cfg);
    if ((err == ESP_OK) || (err == ESP_ERR_NOT_FOUND)) {
        s_echoear_config = cfg;
        ESP_LOGI(
            TAG, "LCD RST selected: GPIO%d (PCB=%s)",
            (int)cfg.lcd_rst, (cfg.pcb_version == ECHOEAR_PCB_V1_2) ? "V1.2" : "V1.0"
        );
        return;
    }

    ESP_LOGW(
        TAG, "PCB detect failed: %s, fallback LCD RST to GPIO%d",
        esp_err_to_name(err), (int)ECHOEAR_LCD_RST_V1_0
    );
}

static void reset_lcd_by_gpio(const echoear_config_t *cfg)
{
    gpio_num_t rst_gpio = cfg ? cfg->lcd_rst : ECHOEAR_LCD_RST_V1_0;
    if (rst_gpio < 0) {
        ESP_LOGW(TAG, "Skip LCD reset: invalid rst gpio");
        return;
    }

    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << rst_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&gpio_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LCD rst gpio_config failed: %s", esp_err_to_name(err));
        return;
    }

    int reset_active_level = (cfg && (cfg->pcb_version == ECHOEAR_PCB_V1_2)) ? 1 : 0;
    int reset_inactive_level = !reset_active_level;
    (void)gpio_set_level(rst_gpio, reset_inactive_level);
    vTaskDelay(pdMS_TO_TICKS(5));
    (void)gpio_set_level(rst_gpio, reset_active_level);
    vTaskDelay(pdMS_TO_TICKS(20));
    (void)gpio_set_level(rst_gpio, reset_inactive_level);
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(
        TAG, "LCD reset pulse sent on GPIO%d (active_level=%d)",
        (int)rst_gpio, reset_active_level
    );
}

static esp_err_t alloc_fill_buffer(void)
{
    if (!s_lcd) {
        return ESP_ERR_INVALID_STATE;
    }

    int width = s_lcd->getFrameWidth();
    int height = s_lcd->getFrameHeight();
    if (width <= 0 || height <= 0) {
        ESP_LOGE(TAG, "Invalid frame size: %dx%d", width, height);
        return ESP_FAIL;
    }

    // SPI panel IO requires DMA-capable source buffers.
    int lines = (height < 32) ? height : 32;
    while (lines > 0) {
        size_t bytes = (size_t)width * (size_t)lines * sizeof(uint16_t);
        s_fill_buffer = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (s_fill_buffer) {
            s_fill_buffer_lines = lines;
            return ESP_OK;
        }
        lines /= 2;
    }

    ESP_LOGE(TAG, "DMA fill buffer allocation failed");
    return ESP_ERR_NO_MEM;
}

static inline uint16_t rgb565_to_panel_bytes(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

static esp_err_t display_panel_fill_rect_rgb565(int x_start, int y_start, int width, int height, uint16_t color)
{
    if (!s_ready || !s_lcd || !s_fill_buffer || (s_fill_buffer_lines <= 0)) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((width <= 0) || (height <= 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t panel_color = rgb565_to_panel_bytes(color);
    size_t chunk_pixels = (size_t)width * (size_t)s_fill_buffer_lines;
    for (size_t index = 0; index < chunk_pixels; ++index) {
        s_fill_buffer[index] = panel_color;
    }

    for (int y = y_start; y < (y_start + height); y += s_fill_buffer_lines) {
        int chunk_height = (y_start + height) - y;
        if (chunk_height > s_fill_buffer_lines) {
            chunk_height = s_fill_buffer_lines;
        }
        if (!s_lcd->drawBitmap(x_start, y, width, chunk_height, (const uint8_t *)s_fill_buffer, -1)) {
            ESP_LOGE(TAG, "drawBitmap failed at x=%d y=%d", x_start, y);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

static esp_err_t display_panel_draw_rgb565_bitmap_reference_scaled(
    int x_start, int y_start, int src_width, int src_height, int scale, const uint16_t *bitmap
)
{
    if (!s_ready || !s_lcd || !s_fill_buffer || (s_fill_buffer_lines <= 0) || (bitmap == nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((src_width <= 0) || (src_height <= 0) || (scale <= 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    int dst_width = src_height * scale;
    int dst_height = src_width * scale;
    if ((dst_width > s_lcd->getFrameWidth()) || (dst_height > s_lcd->getFrameHeight())) {
        ESP_LOGE(TAG, "Scaled logo exceeds frame: %dx%d -> %dx%d", src_width, src_height, dst_width, dst_height);
        return ESP_ERR_INVALID_SIZE;
    }

    for (int y = 0; y < dst_height; y += s_fill_buffer_lines) {
        int chunk_height = dst_height - y;
        if (chunk_height > s_fill_buffer_lines) {
            chunk_height = s_fill_buffer_lines;
        }

        for (int chunk_row = 0; chunk_row < chunk_height; ++chunk_row) {
            int dst_y = y + chunk_row;
            for (int dst_x = 0; dst_x < dst_width; ++dst_x) {
                // Reference upright orientation validated on EchoEar ST77916.
                // Future logos and UI assets should treat this transform as the
                // display-space baseline while the panel keeps SWAP_XY enabled.
                int src_x = src_width - 1 - (dst_y / scale);
                int src_y = dst_x / scale;
                size_t src_index = (size_t)src_y * (size_t)src_width + (size_t)src_x;
                size_t dst_index = (size_t)chunk_row * (size_t)dst_width + (size_t)dst_x;
                s_fill_buffer[dst_index] = rgb565_to_panel_bytes(bitmap[src_index]);
            }
        }

        if (!s_lcd->drawBitmap(
                x_start, y_start + y, dst_width, chunk_height, (const uint8_t *)s_fill_buffer, -1
            )) {
            ESP_LOGE(TAG, "drawBitmap failed for boot logo at x=%d y=%d", x_start, y_start + y);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

extern "C" esp_err_t display_panel_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    configure_echoear_display_power();
    load_echoear_display_config();
    reset_lcd_by_gpio(&s_echoear_config);

    Board *board = new (std::nothrow) Board();
    if (!board) {
        return ESP_ERR_NO_MEM;
    }

    if (!board->init()) {
        ESP_LOGE(TAG, "Board init failed");
        delete board;
        return ESP_FAIL;
    }

    if (!board->begin()) {
        ESP_LOGE(TAG, "Board begin failed");
        board->del();
        delete board;
        return ESP_FAIL;
    }

    LCD *lcd = board->getLCD();
    if (!lcd) {
        ESP_LOGE(TAG, "LCD is not available");
        board->del();
        delete board;
        return ESP_FAIL;
    }

    s_board = board;
    s_lcd = lcd;

    esp_err_t err = alloc_fill_buffer();
    if (err != ESP_OK) {
        s_board->del();
        delete s_board;
        s_board = nullptr;
        s_lcd = nullptr;
        return err;
    }

    auto backlight = s_board->getBacklight();
    if (backlight) {
        if (!backlight->setBrightness(100)) {
            ESP_LOGW(TAG, "Backlight driver setBrightness failed, fallback to GPIO");
            int backlight_on_level = ECHOEAR_LCD_BACKLIGHT_INVERT ? 0 : 1;
            (void)gpio_set_level(ECHOEAR_LCD_BACKLIGHT, backlight_on_level);
        }
    } else {
        ESP_LOGW(TAG, "No backlight driver, fallback to GPIO");
        int backlight_on_level = ECHOEAR_LCD_BACKLIGHT_INVERT ? 0 : 1;
        (void)gpio_set_level(ECHOEAR_LCD_BACKLIGHT, backlight_on_level);
    }

    s_ready = true;
    ESP_LOGI(TAG, "Display initialized: %dx%d", s_lcd->getFrameWidth(), s_lcd->getFrameHeight());
    return ESP_OK;
}

extern "C" esp_err_t display_panel_init_lvgl(void)
{
    if (!s_ready || !s_board || !s_lcd) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_lvgl_ready) {
        return ESP_OK;
    }

    if (!lvgl_port_init(s_lcd, s_board->getTouch())) {
        ESP_LOGE(TAG, "LVGL port init failed");
        return ESP_FAIL;
    }

    s_lvgl_ready = true;
    if (display_panel_lvgl_lock(1000)) {
        lv_disp_t *display = lv_disp_get_default();
        if (display) {
            lv_disp_set_rotation(display, get_reference_rotation());
        }
        display_panel_lvgl_unlock();
    } else {
        ESP_LOGW(TAG, "LVGL lock timeout, skip applying reference rotation");
    }
    ESP_LOGI(TAG, "LVGL initialized");
    return ESP_OK;
}

extern "C" bool display_panel_lvgl_lock(int timeout_ms)
{
    if (!s_lvgl_ready) {
        return false;
    }
    return lvgl_port_lock(timeout_ms);
}

extern "C" bool display_panel_lvgl_unlock(void)
{
    if (!s_lvgl_ready) {
        return false;
    }
    return lvgl_port_unlock();
}

extern "C" bool display_panel_lvgl_is_ready(void)
{
    return s_lvgl_ready;
}

extern "C" esp_err_t display_panel_fill_rgb565(uint16_t color)
{
    if (!s_lcd) {
        return ESP_ERR_INVALID_STATE;
    }

    return display_panel_fill_rect_rgb565(0, 0, s_lcd->getFrameWidth(), s_lcd->getFrameHeight(), color);
}

extern "C" esp_err_t display_panel_show_boot(void)
{
    if (!s_lcd) {
        return ESP_ERR_INVALID_STATE;
    }

    int frame_width = s_lcd->getFrameWidth();
    int frame_height = s_lcd->getFrameHeight();
    static constexpr int kLogoScale = 3;
    // This is the validated reference orientation and size for the current panel setup.
    int logo_width = MINICLAW_LOGO_HEIGHT * kLogoScale;
    int logo_height = MINICLAW_LOGO_WIDTH * kLogoScale;
    int logo_x = (frame_width - logo_width) / 2;
    int logo_y = (frame_height - logo_height) / 2 - 10;
    if (logo_y < 0) {
        logo_y = 0;
    }

    esp_err_t err = display_panel_fill_rgb565(MINICLAW_LOGO_RGB565[0]);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Showing MiniClaw boot logo");
    return display_panel_draw_rgb565_bitmap_reference_scaled(
        logo_x, logo_y, MINICLAW_LOGO_WIDTH, MINICLAW_LOGO_HEIGHT, kLogoScale, MINICLAW_LOGO_RGB565
    );
}

extern "C" bool display_panel_is_ready(void)
{
    return s_ready;
}

extern "C" uint16_t display_panel_get_reference_rotation_degrees(void)
{
    return 270;
}

extern "C" bool display_panel_touch_is_ready(void)
{
    return (s_board != nullptr) && (s_board->getTouch() != nullptr);
}

extern "C" esp_err_t display_panel_touch_read_point(
    uint16_t *x, uint16_t *y, uint16_t *strength, bool *pressed
)
{
    Touch *touch = (s_board != nullptr) ? s_board->getTouch() : nullptr;
    TouchPoint point = {};
    int point_count = 0;

    if (pressed == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *pressed = false;
    if (x != nullptr) {
        *x = 0;
    }
    if (y != nullptr) {
        *y = 0;
    }
    if (strength != nullptr) {
        *strength = 0;
    }

    if (touch == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    point_count = touch->readPoints(&point, 1, 0);
    if (point_count < 0) {
        return ESP_FAIL;
    }
    if (point_count == 0) {
        return ESP_OK;
    }

    if (x != nullptr) {
        *x = (uint16_t)point.x;
    }
    if (y != nullptr) {
        *y = (uint16_t)point.y;
    }
    if (strength != nullptr) {
        *strength = (uint16_t)point.strength;
    }
    *pressed = true;

    return ESP_OK;
}

extern "C" esp_err_t display_panel_touch_get_flags(
    bool *swap_xy, bool *mirror_x, bool *mirror_y, bool *interrupt_enabled
)
{
    Touch *touch = (s_board != nullptr) ? s_board->getTouch() : nullptr;

    if (touch == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    const auto &transformation = touch->getTransformation();
    if (swap_xy != nullptr) {
        *swap_xy = transformation.swap_xy;
    }
    if (mirror_x != nullptr) {
        *mirror_x = transformation.mirror_x;
    }
    if (mirror_y != nullptr) {
        *mirror_y = transformation.mirror_y;
    }
    if (interrupt_enabled != nullptr) {
        *interrupt_enabled = touch->isInterruptEnabled();
    }

    return ESP_OK;
}
