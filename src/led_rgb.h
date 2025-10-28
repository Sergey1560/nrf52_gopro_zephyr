#ifndef GP_LED_RBG_H
#define GP_LED_RBG_H

#include "leds.h"

enum rgb_pixel_name_t{
    RGB_PIXEL_BT = 0,
    RGB_PIXEL_REC = 1,
    RGB_PIXEL_END
};


int led_hw_init(void);
int led_mode_set(enum led_number_t led_num, enum led_mode_t mode);

#endif
