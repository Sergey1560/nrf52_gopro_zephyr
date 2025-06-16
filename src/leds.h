#ifndef GOPRO_LEDS_H
#define GOPRO_LEDS_H

#include <zephyr/kernel.h>

int gopro_leds_init(void);
void gopro_led_set_bt(uint8_t val);
void gopro_led_set_rec(uint8_t val);


#endif