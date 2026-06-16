#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "lcd.h"
#include "camera.h"
#include "env_sensor.h"
#include "ml_classifier.h"

static void show_status(const env_sensor_data_t *data, const ml_classifier_result_t *ml_result)
{
    char line[16] = {0};

    if (data->light_err == ESP_OK) {
        snprintf(line, sizeof(line), "LUX:%7.1f   ", data->lux);
    } else {
        snprintf(line, sizeof(line), "LUX:ERR     ");
    }
    lcd_show_string(2, 1, line, YELLOW, BLACK);

    if (data->humidity_err == ESP_OK) {
        snprintf(line, sizeof(line), "HUM:%6.1f%%  ", data->humidity);
    } else {
        snprintf(line, sizeof(line), "HUM:ERR     ");
    }
    lcd_show_string(3, 1, line, GREEN, BLACK);

    if (data->temperature_err == ESP_OK) {
        snprintf(line, sizeof(line), "TMP:%6.1fC  ", data->temperature_c);
    } else {
        snprintf(line, sizeof(line), "TMP:ERR     ");
    }
    lcd_show_string(4, 1, line, CYAN, BLACK);

    if (ml_result->status == ML_CLASSIFIER_OK && ml_result->confidence >= 0.0f) {
        snprintf(line, sizeof(line), "AI:%s %.0f%%", ml_result->label, ml_result->confidence * 100.0f);
    } else {
        snprintf(line, sizeof(line), "AI:%s", ml_result->label);
    }
    lcd_show_string(6, 1, "               ", WHITE, BLACK);
    lcd_show_string(6, 1, line, WHITE, BLACK);

    snprintf(line, sizeof(line), "T:%4lums      ", (unsigned long)ml_result->inference_ms);
    lcd_show_string(7, 1, line, GRAY, BLACK);
}

void app_main(void)
{
    lcd_init();
    lcd_clear(BLACK);
    lcd_show_string(1, 1, "PLANT AI", WHITE, BLACK);
    env_sensor_init();
    camera_init();
    ml_classifier_init();

    while (1)
    {
        env_sensor_data_t sensor_data = env_sensor_read();
        ml_classifier_result_t ml_result = ml_classifier_run();
        show_status(&sensor_data, &ml_result);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
