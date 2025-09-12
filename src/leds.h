#ifndef GOPRO_LEDS_H
#define GOPRO_LEDS_H

#include <zephyr/kernel.h>

enum led_mode_t{
    LED_MODE_OFF,
    LED_MODE_BLINK_1S,
    LED_MODE_BLINK_5S,
    LED_MODE_BLINK_300MS,
    LED_MODE_BLINK_100MS,
    LED_MODE_ON,
    LED_MODE_END
};

enum led_number_t{
    LED_NUM_REC = 1,
    LED_NUM_BT,
    LED_NUM_END
};

struct led_mode_timing_t{
    k_timeout_t    on_time;
    k_timeout_t    off_time;
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
int gopro_led_mode_set(enum led_number_t led_num, enum led_mode_t mode);
void led_idle_timer_start(uint8_t enable);

#endif