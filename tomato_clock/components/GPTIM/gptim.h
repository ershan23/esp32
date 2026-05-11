#ifndef __GPTIM_H__
#define __GPTIM_H__ 
#include "stdint.h"
extern volatile uint8_t flag_timer;

void gptimer_init(void);
#endif