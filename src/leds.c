#include "leds.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>


#define LED0_NODE	DT_ALIAS(led0)
#if DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#define LED_PRESENT
#endif


#ifdef LED_PRESENT
static struct gpio_dt_spec led_rec = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,{0});
static struct gpio_dt_spec led_bt = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios,{0});


LOG_MODULE_REGISTER(gopro_leds, LOG_LEVEL_DBG);


int gopro_leds_init(void){
	int err;

	if (led_rec.port && !gpio_is_ready_dt(&led_rec)) {
		LOG_ERR("Error: LED device %s is not ready; ignoring it", led_rec.port->name);
		led_rec.port = NULL;
	}
	if (led_rec.port) {
		err = gpio_pin_configure_dt(&led_rec, GPIO_OUTPUT);
		if (err != 0) {
			LOG_ERR("Error %d: failed to configure LED device %s pin %d\n",err, led_rec.port->name, led_rec.pin);
			led_rec.port = NULL;
		} else {
			LOG_INF("Set up LED at %s pin %d\n", led_rec.port->name, led_rec.pin);
		}
	}

	if (led_bt.port && !gpio_is_ready_dt(&led_bt)) {
		LOG_ERR("Error: LED device %s is not ready; ignoring it", led_bt.port->name);
		led_bt.port = NULL;
	}
	if (led_bt.port) {
		err = gpio_pin_configure_dt(&led_bt, GPIO_OUTPUT);
		if (err != 0) {
			LOG_ERR("Error %d: failed to configure LED device %s pin %d\n",err, led_bt.port->name, led_bt.pin);
			led_bt.port = NULL;
		} else {
			LOG_INF("Set up LED at %s pin %d\n", led_bt.port->name, led_bt.pin);
		}
	}

	return 0;
}


void gopro_led_set_bt(uint8_t val){
	if(val){
		gpio_pin_set_dt(&led_bt,1);
	}else{
		gpio_pin_set_dt(&led_bt,0);
	}
}

void gopro_led_set_rec(uint8_t val){
	if(val){
		gpio_pin_set_dt(&led_rec,1);
	}else{
		gpio_pin_set_dt(&led_rec,0);
	}
}


#else
int gopro_leds_init(void){
    return 0;
}

void gopro_led_set_bt(uint8_t val){
}

void gopro_led_set_rec(uint8_t val){
}


#endif
