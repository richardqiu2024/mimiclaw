#include "imu/imu_manager.h"

#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu/bmi270_driver.h"

static const char *TAG = "imu";

static imu_shake_cb_t s_shake_cb = NULL;
static int64_t s_last_shake_us = 0;

static void imu_task_bmi270(void *arg)
{
    (void)arg;
    const float threshold_g = 1.6f;
    const int64_t min_interval_us = 800000;

    while (1) {
        bmi270_accel_t accel;
        esp_err_t err = bmi270_read_accel(&accel);

        if (err == ESP_OK) {
            float mag = sqrtf(accel.x * accel.x + accel.y * accel.y + accel.z * accel.z);
            float delta = fabsf(mag - 1.0f);

            if (delta > threshold_g) {
                int64_t now = esp_timer_get_time();
                if (now - s_last_shake_us > min_interval_us) {
                    s_last_shake_us = now;
                    ESP_LOGI(TAG, "Shake detected (delta=%.2f)", delta);
                    if (s_shake_cb) {
                        s_shake_cb();
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void imu_manager_init(void)
{
    esp_err_t err = bmi270_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Using BMI270 IMU on shared I2C bus");
        xTaskCreatePinnedToCore(imu_task_bmi270, "imu_task", 4096, NULL, 4, NULL, 0);
    } else {
        ESP_LOGE(TAG, "BMI270 initialization failed: %s", esp_err_to_name(err));
    }
}

void imu_manager_set_shake_callback(imu_shake_cb_t cb)
{
    s_shake_cb = cb;
}

bool imu_manager_is_bmi270(void)
{
    return true;
}
