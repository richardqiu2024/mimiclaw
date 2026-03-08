#pragma once

/**
 * EchoEar Hardware Configuration
 *
 * Based on xiaozhi-esp32 project analysis
 * Hardware: EchoEar 喵伴 (ESP32-S3-WROOM-1)
 *
 * Features:
 * - 1.85" QSPI Round Touch Display (ST77916, 360x360)
 * - Dual Microphone Array (ES7210 ADC)
 * - Speaker Output (ES8311 DAC)
 * - Capacitive Touch (CST816S)
 * - Battery Management
 */

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2c.h>

// =============================================================================
// Audio System Configuration (I2S)
// =============================================================================

// Audio Sample Rates
#define ECHOEAR_AUDIO_INPUT_SAMPLE_RATE     24000
#define ECHOEAR_AUDIO_OUTPUT_SAMPLE_RATE    24000
#define ECHOEAR_AUDIO_INPUT_REFERENCE       true    // Enable AEC reference

// I2S Master Interface
#define ECHOEAR_I2S_MCLK                    GPIO_NUM_42
#define ECHOEAR_I2S_WS                      GPIO_NUM_39
#define ECHOEAR_I2S_BCLK                    GPIO_NUM_40
#define ECHOEAR_I2S_DOUT                    GPIO_NUM_41

// I2S Data Input (PCB version dependent)
#define ECHOEAR_I2S_DIN_V1_0                GPIO_NUM_15
#define ECHOEAR_I2S_DIN_V1_2                GPIO_NUM_3

// Power Amplifier Enable (PCB version dependent)
#define ECHOEAR_PA_PIN_V1_0                 GPIO_NUM_4
#define ECHOEAR_PA_PIN_V1_2                 GPIO_NUM_15

// I2C Audio Codec
#define ECHOEAR_AUDIO_I2C_NUM               I2C_NUM_0
#define ECHOEAR_AUDIO_I2C_SDA               GPIO_NUM_2
#define ECHOEAR_AUDIO_I2C_SCL               GPIO_NUM_1
#define ECHOEAR_AUDIO_I2C_FREQ_HZ           100000
// Use a temporary bus instance for board-version probing to avoid runtime conflicts.
#define ECHOEAR_DETECT_I2C_NUM              I2C_NUM_1

// Audio Codec I2C Addresses
#define ECHOEAR_ES8311_ADDR                 0x18    // DAC (Speaker)
#define ECHOEAR_ES7210_ADDR                 0x40    // ADC (Microphone)

// Codec Power Control
#define ECHOEAR_CODEC_POWER_CTRL            GPIO_NUM_48

// =============================================================================
// Display System Configuration (QSPI)
// =============================================================================

// Display Parameters
#define ECHOEAR_DISPLAY_WIDTH               360
#define ECHOEAR_DISPLAY_HEIGHT              360
#define ECHOEAR_DISPLAY_BIT_PER_PIXEL       16      // RGB565
#define ECHOEAR_DISPLAY_MIRROR_X            false
#define ECHOEAR_DISPLAY_MIRROR_Y            false
#define ECHOEAR_DISPLAY_SWAP_XY             false
#define ECHOEAR_DISPLAY_OFFSET_X            0
#define ECHOEAR_DISPLAY_OFFSET_Y            0

// QSPI LCD Interface (ST77916)
#define ECHOEAR_LCD_HOST                    SPI2_HOST
#define ECHOEAR_LCD_PCLK                    GPIO_NUM_18
#define ECHOEAR_LCD_CS                      GPIO_NUM_14
#define ECHOEAR_LCD_DATA0                   GPIO_NUM_46
#define ECHOEAR_LCD_DATA1                   GPIO_NUM_13
#define ECHOEAR_LCD_DATA2                   GPIO_NUM_11
#define ECHOEAR_LCD_DATA3                   GPIO_NUM_12

// LCD Reset (PCB version dependent)
#define ECHOEAR_LCD_RST_V1_0                GPIO_NUM_3
#define ECHOEAR_LCD_RST_V1_2                GPIO_NUM_47

// LCD Backlight (PWM)
#define ECHOEAR_LCD_BACKLIGHT               GPIO_NUM_44
#define ECHOEAR_LCD_BACKLIGHT_INVERT        false

// QSPI DMA Buffer Size
#define ECHOEAR_LCD_DMA_BUFFER_SIZE         (ECHOEAR_DISPLAY_WIDTH * 80 * sizeof(uint16_t))

// =============================================================================
// Touch System Configuration (CST816S)
// =============================================================================

#define ECHOEAR_TOUCH_I2C_NUM               I2C_NUM_0   // Shared with audio
#define ECHOEAR_TOUCH_I2C_ADDR              0x15
#define ECHOEAR_TOUCH_INT                   GPIO_NUM_10
#define ECHOEAR_TOUCH_RST                   GPIO_NUM_NC

// =============================================================================
// Button Configuration
// =============================================================================

#define ECHOEAR_BOOT_BUTTON                 GPIO_NUM_0

// =============================================================================
// Power and Control
// =============================================================================

#define ECHOEAR_POWER_CTRL                  GPIO_NUM_9
#define ECHOEAR_POWER_CTRL_ON_LEVEL         0
#define ECHOEAR_LED_GREEN                   GPIO_NUM_43

// =============================================================================
// Battery Management
// =============================================================================

#define ECHOEAR_CHARGE_IC_ADDR              0x55    // I2C address

// =============================================================================
// UART (PCB version dependent)
// =============================================================================

#define ECHOEAR_UART1_TX_V1_0               GPIO_NUM_6
#define ECHOEAR_UART1_RX_V1_0               GPIO_NUM_5
#define ECHOEAR_UART1_TX_V1_2               GPIO_NUM_5
#define ECHOEAR_UART1_RX_V1_2               GPIO_NUM_4

// =============================================================================
// SD Card (SPI)
// =============================================================================

#define ECHOEAR_SD_MISO                     GPIO_NUM_17
#define ECHOEAR_SD_SCK                      GPIO_NUM_16
#define ECHOEAR_SD_MOSI                     GPIO_NUM_38

// =============================================================================
// Touch Pad (PCB version dependent)
// =============================================================================

#define ECHOEAR_TOUCH_PAD1                  GPIO_NUM_7
#define ECHOEAR_TOUCH_PAD2_V1_0             GPIO_NUM_NC
#define ECHOEAR_TOUCH_PAD2_V1_2             GPIO_NUM_6

// =============================================================================
// PCB Version Detection
// =============================================================================

typedef enum {
    ECHOEAR_PCB_V1_0 = 0,
    ECHOEAR_PCB_V1_2 = 1,
    ECHOEAR_PCB_UNKNOWN = 0xFF
} echoear_pcb_version_t;

// =============================================================================
// Runtime Configuration Structure
// =============================================================================

typedef struct {
    echoear_pcb_version_t pcb_version;
    gpio_num_t i2s_din;
    gpio_num_t pa_pin;
    gpio_num_t lcd_rst;
    gpio_num_t uart1_tx;
    gpio_num_t uart1_rx;
    gpio_num_t touch_pad2;
} echoear_config_t;

// =============================================================================
// Helper Macros
// =============================================================================

#define ECHOEAR_QSPI_BUS_CONFIG(max_trans_sz) {         \
    .data0_io_num = ECHOEAR_LCD_DATA0,                  \
    .data1_io_num = ECHOEAR_LCD_DATA1,                  \
    .sclk_io_num = ECHOEAR_LCD_PCLK,                    \
    .data2_io_num = ECHOEAR_LCD_DATA2,                  \
    .data3_io_num = ECHOEAR_LCD_DATA3,                  \
    .max_transfer_sz = max_trans_sz,                    \
}

#define ECHOEAR_I2C_MASTER_CONFIG() {                   \
    .mode = I2C_MODE_MASTER,                            \
    .sda_io_num = ECHOEAR_AUDIO_I2C_SDA,                \
    .scl_io_num = ECHOEAR_AUDIO_I2C_SCL,                \
    .sda_pullup_en = GPIO_PULLUP_ENABLE,                \
    .scl_pullup_en = GPIO_PULLUP_ENABLE,                \
    .master.clk_speed = ECHOEAR_AUDIO_I2C_FREQ_HZ,      \
}

// =============================================================================
// Function Declarations
// =============================================================================

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Detect EchoEar PCB version
 *
 * @param i2c_num I2C port number
 * @return echoear_pcb_version_t Detected PCB version
 */
echoear_pcb_version_t echoear_detect_pcb_version(i2c_port_t i2c_num);

/**
 * @brief Initialize EchoEar hardware configuration
 *
 * @param config Pointer to configuration structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t echoear_config_init(echoear_config_t *config);

#ifdef __cplusplus
}
#endif
