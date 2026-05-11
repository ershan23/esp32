#include "led.h"
#include "driver/gpio.h"

void led_init(void)
{
    gpio_config_t gpio_cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ull << LED_BLUE) | (1ull << LED_RED) | (1ull << LED_YELLOW) | (1ull << LED_GREEN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&gpio_cfg);
    led_all_off();
}

void led_on(gpio_num_t gpio_num)
{
    gpio_set_level(gpio_num, 0);
}

void led_off(gpio_num_t gpio_num)
{
    gpio_set_level(gpio_num, 1);
}

void led_all_off(void)
{
    led_off(LED_BLUE);
    led_off(LED_RED);
    led_off(LED_YELLOW);
    led_off(LED_GREEN);
}

void gpio_toggle(gpio_num_t gpio_num)
{
    if (gpio_get_level(gpio_num) == 0)
        gpio_set_level(gpio_num, 1);
    else
        gpio_set_level(gpio_num, 0);
}