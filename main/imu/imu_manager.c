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

static void imu_task(void *arg)
{
    (void)arg;
    const float threshold_g = 1.6f;
    const int64_t min_interval_us = 800000;
    esp_err_t last_sample_err = ESP_OK;

    while (1) {
        bmi270_sample_t sample;
        esp_err_t err = bmi270_read_sample(&sample);
        if (err != ESP_OK) {
            if (err != last_sample_err) {
                ESP_LOGW(TAG, "BMI270 sample read failed: %s", esp_err_to_name(err));
                last_sample_err = err;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (last_sample_err != ESP_OK) {
            ESP_LOGI(TAG, "BMI270 sampling recovered");
            last_sample_err = ESP_OK;
        }

        if (!sample.accel_data_ready) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        float ax = sample.accel_x_g;
        float ay = sample.accel_y_g;
        float az = sample.accel_z_g;
        float mag = sqrtf(ax * ax + ay * ay + az * az);
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

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void imu_manager_init(void)
{
    esp_err_t err = bmi270_driver_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Skip IMU init because BMI270 is unavailable: %s", esp_err_to_name(err));
        return;
    }

    xTaskCreatePinnedToCore(imu_task, "imu_task", 4096, NULL, 4, NULL, 0);
}

void imu_manager_set_shake_callback(imu_shake_cb_t cb)
{
    s_shake_cb = cb;
}
