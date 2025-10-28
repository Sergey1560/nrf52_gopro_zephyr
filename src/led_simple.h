#ifndef GP_LEDS_SIMPLE_H
#define GP_LEDS_SIMPLE_H

#include <zephyr/kernel.h>

#ifdef CONFIG_HAS_LED_SIMPLE


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


int led_hw_init(void);
// int gopro_led_mode_set(enum led_number_t led_num, enum led_mode_t mode);
// void led_idle_timer_start(uint8_t enable);

#endif

#endif