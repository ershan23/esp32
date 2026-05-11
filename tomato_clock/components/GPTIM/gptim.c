#include "gptim.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
gptimer_handle_t gptim;
volatile uint8_t flag_timer = 0;
bool IRAM_ATTR TimerCallBack(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx){
    flag_timer = 1;
    return true;
}
void gptimer_init(void){
    gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .flags.intr_shared = 0, 
        .intr_priority=0,
        .resolution_hz = 1000000, 
    };
    gptimer_new_timer(&config, &gptim);

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = 500000,
        .flags.auto_reload_on_alarm = 1,
    };
    gptimer_set_alarm_action(gptim, &alarm_config);

    gptimer_event_callbacks_t gptimer_cb = {
        .on_alarm = TimerCallBack,   
    };

    gptimer_register_event_callbacks(gptim, &gptimer_cb, NULL);

    gptimer_enable(gptim);
    gptimer_start(gptim);

}