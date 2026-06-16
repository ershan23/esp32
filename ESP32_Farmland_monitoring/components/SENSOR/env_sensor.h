#ifndef __MY_ENV_SENSOR_H_
#define __MY_ENV_SENSOR_H_

#include "esp_err.h"

typedef struct {
    float lux;
    float temperature_c;
    float humidity;
    esp_err_t light_err;
    esp_err_t temperature_err;
    esp_err_t humidity_err;
} env_sensor_data_t;

void env_sensor_init(void);
env_sensor_data_t env_sensor_read(void);

#endif
