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
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,{0});


LOG_MODULE_REGISTER(gopro_leds, LOG_LEVEL_DBG);


int gopro_leds_init(void){

	if (led.port && !gpio_is_ready_dt(&led)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",err, led.port->name);
		led.port = NULL;
	}
	if (led.port) {
		err = gpio_pin_configure_dt(&led, GPIO_OUTPUT);
		if (err != 0) {
			LOG_ERR("Error %d: failed to configure LED device %s pin %d\n",err, led.port->name, led.pin);
			led.port = NULL;
		} else {
			LOG_INF("Set up LED at %s pin %d\n", led.port->name, led.pin);
		}
	}

}

#else
int gopro_leds_init(void){
    return 0;
}

#endif
