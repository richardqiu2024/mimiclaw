#pragma once

#include <stdint.h>
#include <string.h>  // For memcpy
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "hardware/echoear_config.h"


/********************* I2C *********************/
#define I2C_Touch_SCL_IO            ECHOEAR_AUDIO_I2C_SCL
#define I2C_Touch_SDA_IO            ECHOEAR_AUDIO_I2C_SDA
#define I2C_MASTER_NUM              ECHOEAR_TOUCH_I2C_NUM
#define I2C_MASTER_FREQ_HZ          ECHOEAR_AUDIO_I2C_FREQ_HZ
#define I2C_MASTER_TX_BUF_DISABLE   0         /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0         /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000


esp_err_t I2C_Init(void);
// Reg addr is 8 bit
esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length);
esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length);
