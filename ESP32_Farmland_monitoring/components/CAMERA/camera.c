#include "camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "lcd.h"
#include "esp_camera.h"
#include "esp_log.h"

static const char *TAG = "camera_app";

#define CAM_PIN_PWDN    GPIO_NUM_10
#define CAM_PIN_RESET   GPIO_NUM_9
#define CAM_PIN_XCLK    GPIO_NUM_NC
#define CAM_PIN_SIOD    GPIO_NUM_39
#define CAM_PIN_SIOC    GPIO_NUM_38

#define CAM_PIN_D7      GPIO_NUM_18
#define CAM_PIN_D6      GPIO_NUM_17
#define CAM_PIN_D5      GPIO_NUM_16
#define CAM_PIN_D4      GPIO_NUM_15
#define CAM_PIN_D3      GPIO_NUM_7
#define CAM_PIN_D2      GPIO_NUM_6
#define CAM_PIN_D1      GPIO_NUM_5
#define CAM_PIN_D0      GPIO_NUM_4
#define CAM_PIN_VSYNC   GPIO_NUM_14
#define CAM_PIN_HREF    GPIO_NUM_3
#define CAM_PIN_PCLK    GPIO_NUM_45

static camera_config_t camera_config = {
    .pin_pwdn  = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = 24000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_RGB565,
    .frame_size = FRAMESIZE_240X240,

    .jpeg_quality = 12, 
    .fb_count = 2, 
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    .fb_location = CAMERA_FB_IN_PSRAM,
};

void camera_init(void){
    gpio_set_level(CAM_PIN_PWDN,0);

    gpio_set_level(CAM_PIN_RESET,0);
    vTaskDelay(20);
    gpio_set_level(CAM_PIN_RESET,1);
    vTaskDelay(20);

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(err));
    }

}

camera_fb_t * fb = NULL;

void camera_show(void){
    
    fb = esp_camera_fb_get();
    if (fb == NULL) {
        ESP_LOGW(TAG, "camera frame is null");
        return;
    }

    lcd_show_picture(fb->buf);

    esp_camera_fb_return(fb);

    fb = NULL;
}
