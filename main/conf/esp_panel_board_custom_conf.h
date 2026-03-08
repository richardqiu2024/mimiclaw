#pragma once

#define ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM  (1)

#if ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM

#define ESP_PANEL_BOARD_NAME                "MimiClaw:ST77916-QSPI"
#define ESP_PANEL_BOARD_WIDTH               (360)
#define ESP_PANEL_BOARD_HEIGHT              (360)

#define ESP_PANEL_BOARD_USE_LCD             (1)

#if ESP_PANEL_BOARD_USE_LCD

#define ESP_PANEL_BOARD_LCD_CONTROLLER      ST77916
#define ESP_PANEL_BOARD_LCD_BUS_TYPE        (ESP_PANEL_BUS_TYPE_QSPI)
#define ESP_PANEL_BOARD_LCD_BUS_SKIP_INIT_HOST      (0)

#define ESP_PANEL_BOARD_LCD_QSPI_HOST_ID    (1)
#if !ESP_PANEL_BOARD_LCD_BUS_SKIP_INIT_HOST
#define ESP_PANEL_BOARD_LCD_QSPI_IO_SCK     (18)
#define ESP_PANEL_BOARD_LCD_QSPI_IO_DATA0   (46)
#define ESP_PANEL_BOARD_LCD_QSPI_IO_DATA1   (13)
#define ESP_PANEL_BOARD_LCD_QSPI_IO_DATA2   (11)
#define ESP_PANEL_BOARD_LCD_QSPI_IO_DATA3   (12)
#endif
#define ESP_PANEL_BOARD_LCD_QSPI_IO_CS      (14)
#define ESP_PANEL_BOARD_LCD_QSPI_MODE       (0)
#define ESP_PANEL_BOARD_LCD_QSPI_CLK_HZ     (40 * 1000 * 1000)
#define ESP_PANEL_BOARD_LCD_QSPI_CMD_BITS   (32)
#define ESP_PANEL_BOARD_LCD_QSPI_PARAM_BITS (8)

#define ESP_PANEL_BOARD_LCD_COLOR_BITS          (ESP_PANEL_LCD_COLOR_BITS_RGB565)
#define ESP_PANEL_BOARD_LCD_COLOR_BGR_ORDER     (0)
#define ESP_PANEL_BOARD_LCD_COLOR_INEVRT_BIT    (1)

#define ESP_PANEL_BOARD_LCD_SWAP_XY             (1)
#define ESP_PANEL_BOARD_LCD_MIRROR_X            (0)
#define ESP_PANEL_BOARD_LCD_MIRROR_Y            (0)
#define ESP_PANEL_BOARD_LCD_GAP_X               (0)
#define ESP_PANEL_BOARD_LCD_GAP_Y               (0)

#define ESP_PANEL_BOARD_LCD_VENDOR_INIT_CMD() \
    { \
        {0xF0, (uint8_t []){0x28}, 1, 0}, \
        {0xF2, (uint8_t []){0x28}, 1, 0}, \
        {0x73, (uint8_t []){0xF0}, 1, 0}, \
        {0x7C, (uint8_t []){0xD1}, 1, 0}, \
        {0x83, (uint8_t []){0xE0}, 1, 0}, \
        {0x84, (uint8_t []){0x61}, 1, 0}, \
        {0xF2, (uint8_t []){0x82}, 1, 0}, \
        {0xF0, (uint8_t []){0x00}, 1, 0}, \
        {0xF0, (uint8_t []){0x01}, 1, 0}, \
        {0xF1, (uint8_t []){0x01}, 1, 0}, \
        {0xB0, (uint8_t []){0x56}, 1, 0}, \
        {0xB1, (uint8_t []){0x4D}, 1, 0}, \
        {0xB2, (uint8_t []){0x24}, 1, 0}, \
        {0xB4, (uint8_t []){0x87}, 1, 0}, \
        {0xB5, (uint8_t []){0x44}, 1, 0}, \
        {0xB6, (uint8_t []){0x8B}, 1, 0}, \
        {0xB7, (uint8_t []){0x40}, 1, 0}, \
        {0xB8, (uint8_t []){0x86}, 1, 0}, \
        {0xBA, (uint8_t []){0x00}, 1, 0}, \
        {0xBB, (uint8_t []){0x08}, 1, 0}, \
        {0xBC, (uint8_t []){0x08}, 1, 0}, \
        {0xBD, (uint8_t []){0x00}, 1, 0}, \
        {0xC0, (uint8_t []){0x80}, 1, 0}, \
        {0xC1, (uint8_t []){0x10}, 1, 0}, \
        {0xC2, (uint8_t []){0x37}, 1, 0}, \
        {0xC3, (uint8_t []){0x80}, 1, 0}, \
        {0xC4, (uint8_t []){0x10}, 1, 0}, \
        {0xC5, (uint8_t []){0x37}, 1, 0}, \
        {0xC6, (uint8_t []){0xA9}, 1, 0}, \
        {0xC7, (uint8_t []){0x41}, 1, 0}, \
        {0xC8, (uint8_t []){0x01}, 1, 0}, \
        {0xC9, (uint8_t []){0xA9}, 1, 0}, \
        {0xCA, (uint8_t []){0x41}, 1, 0}, \
        {0xCB, (uint8_t []){0x01}, 1, 0}, \
        {0xD0, (uint8_t []){0x91}, 1, 0}, \
        {0xD1, (uint8_t []){0x68}, 1, 0}, \
        {0xD2, (uint8_t []){0x68}, 1, 0}, \
        {0xF5, (uint8_t []){0x00, 0xA5}, 2, 0}, \
        {0xDD, (uint8_t []){0x4F}, 1, 0}, \
        {0xDE, (uint8_t []){0x4F}, 1, 0}, \
        {0xF1, (uint8_t []){0x10}, 1, 0}, \
        {0xF0, (uint8_t []){0x00}, 1, 0}, \
        {0xF0, (uint8_t []){0x02}, 1, 0}, \
        {0xE0, (uint8_t []){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0}, \
        {0xE1, (uint8_t []){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0}, \
        {0xF0, (uint8_t []){0x10}, 1, 0}, \
        {0xF3, (uint8_t []){0x10}, 1, 0}, \
        {0xE0, (uint8_t []){0x07}, 1, 0}, \
        {0xE1, (uint8_t []){0x00}, 1, 0}, \
        {0xE2, (uint8_t []){0x00}, 1, 0}, \
        {0xE3, (uint8_t []){0x00}, 1, 0}, \
        {0xE4, (uint8_t []){0xE0}, 1, 0}, \
        {0xE5, (uint8_t []){0x06}, 1, 0}, \
        {0xE6, (uint8_t []){0x21}, 1, 0}, \
        {0xE7, (uint8_t []){0x01}, 1, 0}, \
        {0xE8, (uint8_t []){0x05}, 1, 0}, \
        {0xE9, (uint8_t []){0x02}, 1, 0}, \
        {0xEA, (uint8_t []){0xDA}, 1, 0}, \
        {0xEB, (uint8_t []){0x00}, 1, 0}, \
        {0xEC, (uint8_t []){0x00}, 1, 0}, \
        {0xED, (uint8_t []){0x0F}, 1, 0}, \
        {0xEE, (uint8_t []){0x00}, 1, 0}, \
        {0xEF, (uint8_t []){0x00}, 1, 0}, \
        {0xF8, (uint8_t []){0x00}, 1, 0}, \
        {0xF9, (uint8_t []){0x00}, 1, 0}, \
        {0xFA, (uint8_t []){0x00}, 1, 0}, \
        {0xFB, (uint8_t []){0x00}, 1, 0}, \
        {0xFC, (uint8_t []){0x00}, 1, 0}, \
        {0xFD, (uint8_t []){0x00}, 1, 0}, \
        {0xFE, (uint8_t []){0x00}, 1, 0}, \
        {0xFF, (uint8_t []){0x00}, 1, 0}, \
        {0x60, (uint8_t []){0x40}, 1, 0}, \
        {0x61, (uint8_t []){0x04}, 1, 0}, \
        {0x62, (uint8_t []){0x00}, 1, 0}, \
        {0x63, (uint8_t []){0x42}, 1, 0}, \
        {0x64, (uint8_t []){0xD9}, 1, 0}, \
        {0x65, (uint8_t []){0x00}, 1, 0}, \
        {0x66, (uint8_t []){0x00}, 1, 0}, \
        {0x67, (uint8_t []){0x00}, 1, 0}, \
        {0x68, (uint8_t []){0x00}, 1, 0}, \
        {0x69, (uint8_t []){0x00}, 1, 0}, \
        {0x6A, (uint8_t []){0x00}, 1, 0}, \
        {0x6B, (uint8_t []){0x00}, 1, 0}, \
        {0x70, (uint8_t []){0x40}, 1, 0}, \
        {0x71, (uint8_t []){0x03}, 1, 0}, \
        {0x72, (uint8_t []){0x00}, 1, 0}, \
        {0x73, (uint8_t []){0x42}, 1, 0}, \
        {0x74, (uint8_t []){0xD8}, 1, 0}, \
        {0x75, (uint8_t []){0x00}, 1, 0}, \
        {0x76, (uint8_t []){0x00}, 1, 0}, \
        {0x77, (uint8_t []){0x00}, 1, 0}, \
        {0x78, (uint8_t []){0x00}, 1, 0}, \
        {0x79, (uint8_t []){0x00}, 1, 0}, \
        {0x7A, (uint8_t []){0x00}, 1, 0}, \
        {0x7B, (uint8_t []){0x00}, 1, 0}, \
        {0x80, (uint8_t []){0x48}, 1, 0}, \
        {0x81, (uint8_t []){0x00}, 1, 0}, \
        {0x82, (uint8_t []){0x06}, 1, 0}, \
        {0x83, (uint8_t []){0x02}, 1, 0}, \
        {0x84, (uint8_t []){0xD6}, 1, 0}, \
        {0x85, (uint8_t []){0x04}, 1, 0}, \
        {0x86, (uint8_t []){0x00}, 1, 0}, \
        {0x87, (uint8_t []){0x00}, 1, 0}, \
        {0x88, (uint8_t []){0x48}, 1, 0}, \
        {0x89, (uint8_t []){0x00}, 1, 0}, \
        {0x8A, (uint8_t []){0x08}, 1, 0}, \
        {0x8B, (uint8_t []){0x02}, 1, 0}, \
        {0x8C, (uint8_t []){0xD8}, 1, 0}, \
        {0x8D, (uint8_t []){0x04}, 1, 0}, \
        {0x8E, (uint8_t []){0x00}, 1, 0}, \
        {0x8F, (uint8_t []){0x00}, 1, 0}, \
        {0x90, (uint8_t []){0x48}, 1, 0}, \
        {0x91, (uint8_t []){0x00}, 1, 0}, \
        {0x92, (uint8_t []){0x0A}, 1, 0}, \
        {0x93, (uint8_t []){0x02}, 1, 0}, \
        {0x94, (uint8_t []){0xDA}, 1, 0}, \
        {0x95, (uint8_t []){0x04}, 1, 0}, \
        {0x96, (uint8_t []){0x00}, 1, 0}, \
        {0x97, (uint8_t []){0x00}, 1, 0}, \
        {0x98, (uint8_t []){0x48}, 1, 0}, \
        {0x99, (uint8_t []){0x00}, 1, 0}, \
        {0x9A, (uint8_t []){0x0C}, 1, 0}, \
        {0x9B, (uint8_t []){0x02}, 1, 0}, \
        {0x9C, (uint8_t []){0xDC}, 1, 0}, \
        {0x9D, (uint8_t []){0x04}, 1, 0}, \
        {0x9E, (uint8_t []){0x00}, 1, 0}, \
        {0x9F, (uint8_t []){0x00}, 1, 0}, \
        {0xA0, (uint8_t []){0x48}, 1, 0}, \
        {0xA1, (uint8_t []){0x00}, 1, 0}, \
        {0xA2, (uint8_t []){0x05}, 1, 0}, \
        {0xA3, (uint8_t []){0x02}, 1, 0}, \
        {0xA4, (uint8_t []){0xD5}, 1, 0}, \
        {0xA5, (uint8_t []){0x04}, 1, 0}, \
        {0xA6, (uint8_t []){0x00}, 1, 0}, \
        {0xA7, (uint8_t []){0x00}, 1, 0}, \
        {0xA8, (uint8_t []){0x48}, 1, 0}, \
        {0xA9, (uint8_t []){0x00}, 1, 0}, \
        {0xAA, (uint8_t []){0x07}, 1, 0}, \
        {0xAB, (uint8_t []){0x02}, 1, 0}, \
        {0xAC, (uint8_t []){0xD7}, 1, 0}, \
        {0xAD, (uint8_t []){0x04}, 1, 0}, \
        {0xAE, (uint8_t []){0x00}, 1, 0}, \
        {0xAF, (uint8_t []){0x00}, 1, 0}, \
        {0xB0, (uint8_t []){0x48}, 1, 0}, \
        {0xB1, (uint8_t []){0x00}, 1, 0}, \
        {0xB2, (uint8_t []){0x09}, 1, 0}, \
        {0xB3, (uint8_t []){0x02}, 1, 0}, \
        {0xB4, (uint8_t []){0xD9}, 1, 0}, \
        {0xB5, (uint8_t []){0x04}, 1, 0}, \
        {0xB6, (uint8_t []){0x00}, 1, 0}, \
        {0xB7, (uint8_t []){0x00}, 1, 0}, \
        {0xB8, (uint8_t []){0x48}, 1, 0}, \
        {0xB9, (uint8_t []){0x00}, 1, 0}, \
        {0xBA, (uint8_t []){0x0B}, 1, 0}, \
        {0xBB, (uint8_t []){0x02}, 1, 0}, \
        {0xBC, (uint8_t []){0xDB}, 1, 0}, \
        {0xBD, (uint8_t []){0x04}, 1, 0}, \
        {0xBE, (uint8_t []){0x00}, 1, 0}, \
        {0xBF, (uint8_t []){0x00}, 1, 0}, \
        {0xC0, (uint8_t []){0x10}, 1, 0}, \
        {0xC1, (uint8_t []){0x47}, 1, 0}, \
        {0xC2, (uint8_t []){0x56}, 1, 0}, \
        {0xC3, (uint8_t []){0x65}, 1, 0}, \
        {0xC4, (uint8_t []){0x74}, 1, 0}, \
        {0xC5, (uint8_t []){0x88}, 1, 0}, \
        {0xC6, (uint8_t []){0x99}, 1, 0}, \
        {0xC7, (uint8_t []){0x01}, 1, 0}, \
        {0xC8, (uint8_t []){0xBB}, 1, 0}, \
        {0xC9, (uint8_t []){0xAA}, 1, 0}, \
        {0xD0, (uint8_t []){0x10}, 1, 0}, \
        {0xD1, (uint8_t []){0x47}, 1, 0}, \
        {0xD2, (uint8_t []){0x56}, 1, 0}, \
        {0xD3, (uint8_t []){0x65}, 1, 0}, \
        {0xD4, (uint8_t []){0x74}, 1, 0}, \
        {0xD5, (uint8_t []){0x88}, 1, 0}, \
        {0xD6, (uint8_t []){0x99}, 1, 0}, \
        {0xD7, (uint8_t []){0x01}, 1, 0}, \
        {0xD8, (uint8_t []){0xBB}, 1, 0}, \
        {0xD9, (uint8_t []){0xAA}, 1, 0}, \
        {0xF3, (uint8_t []){0x01}, 1, 0}, \
        {0xF0, (uint8_t []){0x00}, 1, 0}, \
        {0x21, (uint8_t []){0x00}, 1, 0}, \
        {0x11, (uint8_t []){0x00}, 1, 120}, \
        {0x29, (uint8_t []){0x00}, 1, 0}, \
    }

// LCD reset is selected at runtime (EchoEar V1.0: GPIO3, V1.2: GPIO47).
// Keep default config reset disabled to avoid driving a wrong pin.
#define ESP_PANEL_BOARD_LCD_RST_IO              (-1)
#define ESP_PANEL_BOARD_LCD_RST_LEVEL           (0)

#endif

#define ESP_PANEL_BOARD_USE_TOUCH               (0)

#define ESP_PANEL_BOARD_USE_BACKLIGHT           (1)

#if ESP_PANEL_BOARD_USE_BACKLIGHT
#define ESP_PANEL_BOARD_BACKLIGHT_TYPE          (ESP_PANEL_BACKLIGHT_TYPE_PWM_LEDC)
#define ESP_PANEL_BOARD_BACKLIGHT_IO            (44)
#define ESP_PANEL_BOARD_BACKLIGHT_ON_LEVEL      (1)
#define ESP_PANEL_BOARD_BACKLIGHT_PWM_FREQ_HZ   (25000)
#define ESP_PANEL_BOARD_BACKLIGHT_PWM_DUTY_RESOLUTION  (10)
#define ESP_PANEL_BOARD_BACKLIGHT_IDLE_OFF      (0)
#endif

#endif

#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MAJOR 1
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MINOR 2
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_PATCH 0
