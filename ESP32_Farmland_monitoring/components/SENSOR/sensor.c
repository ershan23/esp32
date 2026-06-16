#include "env_sensor.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SENSOR_I2C_PORT              I2C_NUM_0
#define SENSOR_I2C_SDA               GPIO_NUM_41
#define SENSOR_I2C_SCL               GPIO_NUM_42
#define SENSOR_I2C_FREQ_HZ           100000
#define SENSOR_I2C_TIMEOUT_MS        100

#define BH1750_ADDR_LOW              0x23
#define BH1750_ADDR_HIGH             0x5C
#define BH1750_POWER_ON              0x01
#define BH1750_CONT_HIGH_RES_MODE    0x10

#define DHT22_DATA_GPIO              GPIO_NUM_2
#define DHT22_MIN_INTERVAL_US        2000000
#define DHT22_START_LOW_US           2000
#define DHT22_BIT_HIGH_THRESHOLD_US  50

static const char *TAG = "sensor";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_bh1750_dev;
static int64_t s_last_dht22_read_us;
static env_sensor_data_t s_last_dht22_data;

static esp_err_t bh1750_write_cmd(uint8_t cmd)
{
    return i2c_master_transmit(s_bh1750_dev, &cmd, 1, SENSOR_I2C_TIMEOUT_MS);
}

static esp_err_t bh1750_add_device(uint8_t addr)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = SENSOR_I2C_FREQ_HZ,
    };

    return i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_bh1750_dev);
}

static esp_err_t bh1750_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = SENSOR_I2C_PORT,
        .sda_io_num = SENSOR_I2C_SDA,
        .scl_io_num = SENSOR_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus), TAG, "init i2c bus failed");

    ESP_RETURN_ON_ERROR(bh1750_add_device(BH1750_ADDR_LOW), TAG, "add bh1750 failed");
    esp_err_t err = bh1750_write_cmd(BH1750_POWER_ON);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(s_bh1750_dev);
        s_bh1750_dev = NULL;
        ESP_RETURN_ON_ERROR(bh1750_add_device(BH1750_ADDR_HIGH), TAG, "add bh1750 high address failed");
        err = bh1750_write_cmd(BH1750_POWER_ON);
        if (err != ESP_OK) {
            i2c_master_bus_rm_device(s_bh1750_dev);
            s_bh1750_dev = NULL;
            return err;
        }
    }

    ESP_RETURN_ON_ERROR(bh1750_write_cmd(BH1750_CONT_HIGH_RES_MODE), TAG, "set bh1750 mode failed");
    vTaskDelay(pdMS_TO_TICKS(180));
    return ESP_OK;
}

static esp_err_t bh1750_read_lux(float *lux)
{
    uint8_t data[2] = {0};
    esp_err_t err = i2c_master_receive(s_bh1750_dev, data, sizeof(data), SENSOR_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    *lux = (float)raw / 1.2f;
    return ESP_OK;
}

static esp_err_t dht22_wait_level(int level, uint32_t timeout_us, uint32_t *duration_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(DHT22_DATA_GPIO) == level) {
        int64_t elapsed = esp_timer_get_time() - start;
        if (elapsed > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    if (duration_us != NULL) {
        *duration_us = (uint32_t)(esp_timer_get_time() - start);
    }
    return ESP_OK;
}

static esp_err_t dht22_init(void)
{
    gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << DHT22_DATA_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "config dht22 gpio failed");
    gpio_set_level(DHT22_DATA_GPIO, 1);

    s_last_dht22_data.temperature_err = ESP_ERR_INVALID_STATE;
    s_last_dht22_data.humidity_err = ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

static esp_err_t dht22_read(float *temperature_c, float *humidity)
{
    int64_t now = esp_timer_get_time();
    if ((now - s_last_dht22_read_us) < DHT22_MIN_INTERVAL_US) {
        if (s_last_dht22_data.temperature_err == ESP_OK && s_last_dht22_data.humidity_err == ESP_OK) {
            *temperature_c = s_last_dht22_data.temperature_c;
            *humidity = s_last_dht22_data.humidity;
            return ESP_OK;
        }
    }

    uint8_t bytes[5] = {0};

    gpio_set_direction(DHT22_DATA_GPIO, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(DHT22_DATA_GPIO, 0);
    esp_rom_delay_us(DHT22_START_LOW_US);
    gpio_set_level(DHT22_DATA_GPIO, 1);
    esp_rom_delay_us(40);
    gpio_set_direction(DHT22_DATA_GPIO, GPIO_MODE_INPUT);

    esp_err_t err = dht22_wait_level(1, 100, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = dht22_wait_level(0, 100, NULL);
    if (err != ESP_OK) {
        return err;
    }

    for (int bit = 0; bit < 40; bit++) {
        uint32_t high_us = 0;
        err = dht22_wait_level(1, 100, NULL);
        if (err != ESP_OK) {
            return err;
        }
        err = dht22_wait_level(0, 120, &high_us);
        if (err != ESP_OK) {
            return err;
        }
        if (high_us > DHT22_BIT_HIGH_THRESHOLD_US) {
            bytes[bit / 8] |= (1 << (7 - (bit % 8)));
        }
    }

    uint8_t checksum = bytes[0] + bytes[1] + bytes[2] + bytes[3];
    if (checksum != bytes[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_humidity = ((uint16_t)bytes[0] << 8) | bytes[1];
    uint16_t raw_temperature = ((uint16_t)(bytes[2] & 0x7F) << 8) | bytes[3];
    if (raw_humidity > 1000) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *humidity = raw_humidity / 10.0f;
    *temperature_c = raw_temperature / 10.0f;
    if (bytes[2] & 0x80) {
        *temperature_c = -*temperature_c;
    }

    s_last_dht22_read_us = now;
    s_last_dht22_data.temperature_c = *temperature_c;
    s_last_dht22_data.humidity = *humidity;
    s_last_dht22_data.temperature_err = ESP_OK;
    s_last_dht22_data.humidity_err = ESP_OK;
    return ESP_OK;
}

void env_sensor_init(void)
{
    esp_err_t light_err = bh1750_init();
    if (light_err != ESP_OK) {
        ESP_LOGW(TAG, "BH1750 init failed: %s", esp_err_to_name(light_err));
    }

    esp_err_t dht22_err = dht22_init();
    if (dht22_err != ESP_OK) {
        ESP_LOGW(TAG, "DHT22 init failed: %s", esp_err_to_name(dht22_err));
    }
}

env_sensor_data_t env_sensor_read(void)
{
    env_sensor_data_t data = {
        .lux = 0.0f,
        .temperature_c = 0.0f,
        .humidity = 0.0f,
        .light_err = ESP_ERR_INVALID_STATE,
        .temperature_err = ESP_ERR_INVALID_STATE,
        .humidity_err = ESP_ERR_INVALID_STATE,
    };

    if (s_bh1750_dev != NULL) {
        data.light_err = bh1750_read_lux(&data.lux);
    }

    esp_err_t dht_err = dht22_read(&data.temperature_c, &data.humidity);
    data.temperature_err = dht_err;
    data.humidity_err = dht_err;

    return data;
}
