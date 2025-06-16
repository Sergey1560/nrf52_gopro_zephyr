#ifndef GOPRO_LEDS_H
#define GOPRO_LEDS_H

#include <zephyr/kernel.h>

enum led_mode_t{
    LED_MODE_OFF,
    LED_MODE_BLINK_1S,
    LED_MODE_BLINK_300MS,
    LED_MODE_BLINK_100MS,
    LED_MODE_ON,
    LED_MODE_END
};


struct led_message_t{    
    union {
        uint8_t value;
        struct 
        {
            uint8_t mode:3;
            uint8_t led_number:5;
        };
    };
};




int gopro_leds_init(void);
void gopro_led_set_bt(uint8_t val);
void gopro_led_set_rec(uint8_t val);


#endif