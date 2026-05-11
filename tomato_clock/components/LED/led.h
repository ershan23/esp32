#ifndef __LED_H_
#define __LED_H_

#include "driver/gpio.h"

#define LED_BLUE    GPIO_NUM_38
#define LED_RED     GPIO_NUM_39
#define LED_YELLOW  GPIO_NUM_41
#define LED_GREEN   GPIO_NUM_42

void led_init(void);
void led_on(gpio_num_t gpio_num);
void led_off(gpio_num_t gpio_num);
void led_all_off(void);
void gpio_toggle(gpio_num_t gpio_num);
#endif
