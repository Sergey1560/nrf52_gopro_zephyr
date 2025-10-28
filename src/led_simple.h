#ifndef GP_LEDS_SIMPLE_H
#define GP_LEDS_SIMPLE_H

#include "leds.h"

#ifdef CONFIG_HAS_LED_SIMPLE

enum simple_led_name_t{
    SIMPLE_LED_REC = 0,
    SIMPLE_LED_BT = 1,
    SIMPLE_LED_END
};


int led_hw_init(void);
int led_mode_set(enum led_number_t led_num, enum led_mode_t mode);

#endif

#endif