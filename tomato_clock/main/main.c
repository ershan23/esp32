#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "lcd.h"
#include "led.h"
#include "gptim.h"

#define KEY1_GPIO           GPIO_NUM_9

#define WORK_SEC            (25 * 60)
#define SHORT_BREAK_SEC     (5 * 60)
#define LONG_BREAK_SEC      (15 * 60)

typedef enum {
    STATE_READY,
    STATE_WORK,
    STATE_WORK_DONE,
    STATE_SHORT_BREAK,
    STATE_SHORT_BREAK_DONE,
    STATE_LONG_BREAK,
    STATE_LONG_BREAK_DONE,
} pomodoro_state_t;

static pomodoro_state_t state = STATE_READY;
static uint16_t timer_seconds = 0;
static uint8_t work_sessions = 0;
static bool key_pressed = false;
static uint8_t half_sec_count = 0;
static uint16_t prev_timer_seconds = 0xFFFF;

static uint8_t center_col(uint8_t len)
{
    return (15 - len) / 2 + 1;
}

static void key_init(void)
{
    gpio_config_t cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ull << KEY1_GPIO,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
}

static bool key_is_pressed(void)
{
    return gpio_get_level(KEY1_GPIO) == 0;
}

static void set_led_for_state(pomodoro_state_t s)
{
    led_all_off();
    switch (s) {
        case STATE_READY:
            led_on(LED_BLUE);
            break;
        case STATE_WORK:
        case STATE_WORK_DONE:
            led_on(LED_RED);
            break;
        case STATE_SHORT_BREAK:
        case STATE_SHORT_BREAK_DONE:
            led_on(LED_YELLOW);
            break;
        case STATE_LONG_BREAK:
        case STATE_LONG_BREAK_DONE:
            led_on(LED_GREEN);
            break;
    }
}

static void display_time(uint8_t line, uint8_t col, uint16_t seconds, uint16_t fg, uint16_t bg)
{
    uint8_t min = seconds / 60;
    uint8_t sec = seconds % 60;
    lcd_show_num(line, col,     min / 10, 1, fg, bg);
    lcd_show_num(line, col + 1, min % 10, 1, fg, bg);
    lcd_show_char(line, col + 2, ':', fg, bg);
    lcd_show_num(line, col + 3, sec / 10, 1, fg, bg);
    lcd_show_num(line, col + 4, sec % 10, 1, fg, bg);
}

static void clear_line(uint8_t line)
{
    lcd_show_string(line, 1, "               ", WHITE, BLACK);
}

static void display_static(pomodoro_state_t s)
{
    lcd_clear(BLACK);

    switch (s) {
        case STATE_READY:
            lcd_show_string(1, center_col(8),  "POMODORO",  YELLOW, BLACK);
            lcd_show_string(4, center_col(10), "PRESS KEY1", CYAN,  BLACK);
            lcd_show_string(5, center_col(8),  "TO START",  CYAN,  BLACK);
            break;
        case STATE_WORK: {
            lcd_show_string(1, center_col(9),  "WORK TIME", RED,   BLACK);
            char buf[8];
            sprintf(buf, "%d / 4", work_sessions + 1);
            lcd_show_string(2, center_col(strlen(buf)), buf, CYAN, BLACK);
            display_time(3, center_col(5), WORK_SEC, WHITE, BLACK);
            break;
        }
        case STATE_WORK_DONE:
            lcd_show_string(1, center_col(10), "WORK DONE!", GREEN, BLACK);
            lcd_show_string(4, center_col(10), "PRESS KEY1", CYAN,  BLACK);
            break;
        case STATE_SHORT_BREAK:
            lcd_show_string(1, center_col(11), "SHORT BREAK", YELLOW, BLACK);
            display_time(3, center_col(5), SHORT_BREAK_SEC, WHITE, BLACK);
            break;
        case STATE_SHORT_BREAK_DONE:
            lcd_show_string(1, center_col(11), "BREAK OVER!", GREEN, BLACK);
            lcd_show_string(4, center_col(10), "PRESS KEY1",  CYAN,  BLACK);
            break;
        case STATE_LONG_BREAK:
            lcd_show_string(1, center_col(10), "LONG BREAK", GREEN, BLACK);
            display_time(3, center_col(5), LONG_BREAK_SEC, WHITE, BLACK);
            break;
        case STATE_LONG_BREAK_DONE:
            lcd_show_string(1, center_col(11), "BREAK OVER!", GREEN, BLACK);
            lcd_show_string(4, center_col(10), "PRESS KEY1",  CYAN,  BLACK);
            break;
    }
}

static void update_timer_display(void)
{
    if (timer_seconds == prev_timer_seconds)
        return;
    prev_timer_seconds = timer_seconds;

    uint16_t fg = WHITE;
    if (state == STATE_WORK && timer_seconds <= 60)
        fg = YELLOW;
    else if (state == STATE_WORK && timer_seconds <= 300)
        fg = MAGENTA;

    display_time(3, center_col(5), timer_seconds, fg, BLACK);
}

static void transition_to(pomodoro_state_t new_state)
{
    state = new_state;
    set_led_for_state(new_state);
    display_static(new_state);

    switch (new_state) {
        case STATE_READY:
            timer_seconds = 0;
            break;
        case STATE_WORK:
            timer_seconds = WORK_SEC;
            prev_timer_seconds = 0xFFFF;
            break;
        case STATE_SHORT_BREAK:
            timer_seconds = SHORT_BREAK_SEC;
            prev_timer_seconds = 0xFFFF;
            break;
        case STATE_LONG_BREAK:
            timer_seconds = LONG_BREAK_SEC;
            prev_timer_seconds = 0xFFFF;
            break;
        default:
            timer_seconds = 0;
            prev_timer_seconds = 0xFFFF;
            break;
    }
}

static void handle_key_action(void)
{
    switch (state) {
        case STATE_READY:
            transition_to(STATE_WORK);
            break;
        case STATE_WORK_DONE:
            work_sessions++;
            if (work_sessions >= 4) {
                work_sessions = 0;
                transition_to(STATE_LONG_BREAK);
            } else {
                transition_to(STATE_SHORT_BREAK);
            }
            break;
        case STATE_SHORT_BREAK_DONE:
            transition_to(STATE_WORK);
            break;
        case STATE_LONG_BREAK_DONE:
            transition_to(STATE_READY);
            break;
        default:
            break;
    }
}

void app_main(void)
{
    lcd_init();
    led_init();
    key_init();
    gptimer_init();

    transition_to(STATE_READY);

    while (1) {
        while (flag_timer == 0) {
            if (key_is_pressed() && !key_pressed) {
                key_pressed = true;
                vTaskDelay(pdMS_TO_TICKS(50));
                if (key_is_pressed()) {
                    handle_key_action();
                }
            } else if (!key_is_pressed()) {
                key_pressed = false;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        flag_timer = 0;

        switch (state) {
            case STATE_WORK:
            case STATE_SHORT_BREAK:
            case STATE_LONG_BREAK:
                half_sec_count++;
                if (half_sec_count >= 2) {
                    half_sec_count = 0;
                    if (timer_seconds > 0) {
                        timer_seconds--;
                        update_timer_display();
                        if (timer_seconds == 0) {
                            switch (state) {
                                case STATE_WORK:
                                    transition_to(STATE_WORK_DONE);
                                    break;
                                case STATE_SHORT_BREAK:
                                    transition_to(STATE_SHORT_BREAK_DONE);
                                    break;
                                case STATE_LONG_BREAK:
                                    transition_to(STATE_LONG_BREAK_DONE);
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
                }
                break;
            default:
                break;
        }
    }
}
