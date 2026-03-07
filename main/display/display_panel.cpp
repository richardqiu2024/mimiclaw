#include "display/display_panel.h"

#include <new>

#include "esp_display_panel.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"

using namespace esp_panel::board;
using namespace esp_panel::drivers;

static const char *TAG = "display_panel";

static Board *s_board = nullptr;
static LCD *s_lcd = nullptr;
static uint16_t *s_framebuffer = nullptr;
static size_t s_pixel_count = 0;
static bool s_ready = false;

static esp_err_t alloc_framebuffer(void)
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

    s_pixel_count = (size_t)width * (size_t)height;
    size_t bytes = s_pixel_count * sizeof(uint16_t);

    s_framebuffer = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_framebuffer) {
        s_framebuffer = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    if (!s_framebuffer) {
        ESP_LOGE(TAG, "Framebuffer allocation failed (%u bytes)", (unsigned)bytes);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

extern "C" esp_err_t display_panel_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

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

    esp_err_t err = alloc_framebuffer();
    if (err != ESP_OK) {
        s_board->del();
        delete s_board;
        s_board = nullptr;
        s_lcd = nullptr;
        return err;
    }

    auto backlight = s_board->getBacklight();
    if (backlight) {
        backlight->setBrightness(100);
    }

    s_ready = true;
    ESP_LOGI(TAG, "Display initialized: %dx%d", s_lcd->getFrameWidth(), s_lcd->getFrameHeight());
    return ESP_OK;
}

extern "C" esp_err_t display_panel_fill_rgb565(uint16_t color)
{
    if (!s_ready || !s_lcd || !s_framebuffer) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t index = 0; index < s_pixel_count; ++index) {
        s_framebuffer[index] = color;
    }

    int width = s_lcd->getFrameWidth();
    int height = s_lcd->getFrameHeight();
    if (!s_lcd->drawBitmap(0, 0, width, height, (const uint8_t *)s_framebuffer, -1)) {
        ESP_LOGE(TAG, "drawBitmap failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

extern "C" esp_err_t display_panel_show_boot(void)
{
    return display_panel_fill_rgb565(0x001F);
}

extern "C" bool display_panel_is_ready(void)
{
    return s_ready;
}
