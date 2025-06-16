#include "leds.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#define LED0_NODE	DT_ALIAS(led0)
#if DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#define LED_PRESENT
#endif


#ifdef LED_PRESENT
LOG_MODULE_REGISTER(gopro_leds, LOG_LEVEL_DBG);

static struct gpio_dt_spec led_rec = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,{0});
static struct gpio_dt_spec led_bt = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios,{0});

ZBUS_CHAN_DEFINE(leds_chan,                           	/* Name */
         struct led_message_t,                       		      	/* Message type */
         NULL,                                       	/* Validator */
         NULL,                                       	/* User Data */
         ZBUS_OBSERVERS(leds_obs),  	        		/* observers */
         ZBUS_MSG_INIT(0)       						/* Initial value */
);

ZBUS_SUBSCRIBER_DEFINE(leds_obs, 4);

void leds_obs_task(void){
	const struct zbus_channel *chan;
	struct led_message_t led_message;
	struct gpio_dt_spec *led;
	
	int prescaler = 0;

	led_message.value = 0;
	
	while(1){
		if(zbus_sub_wait(&leds_obs, &chan, K_MSEC(100)) == 0 ){//Notification recv
			if (&leds_chan == chan) {
				zbus_chan_read(&leds_chan, &led_message, K_NO_WAIT);
			}
		}
		
		switch (led_message.led_number)
		{
		case 1:
			led = &led_bt;
			break;

		case 2:
			led = &led_rec;
			break;
		
		default:
			led = NULL;
			break;
		}

		if(led != NULL){

			switch (led_message.mode)
			{
			case LED_MODE_OFF:
				gpio_pin_set_dt(led,0);
				break;

			case LED_MODE_ON:
				gpio_pin_set_dt(led,1);
				break;

			case LED_MODE_BLINK_100MS:
				gpio_pin_toggle_dt(led);
				break;

			case LED_MODE_BLINK_300MS:
				prescaler++;
				if(prescaler > 3){
					prescaler = 0;
					gpio_pin_toggle_dt(led);	
				}
				break;

			case LED_MODE_BLINK_1S:
				prescaler++;
				if(prescaler > 10){
					prescaler = 0;
					gpio_pin_toggle_dt(led);	
				}
				break;

				default:
				break;
			}
		}
	}
	
	
	// while (!zbus_sub_wait(&leds_obs, &chan, K_FOREVER)) {
	// 		struct led_message_t led_message;

	// 		if (&leds_chan == chan) {
	// 				// Indirect message access
	// 				zbus_chan_read(&leds_chan, &led_message, K_NO_WAIT);
	// 				LOG_DBG("Set Mode %d for Led %d", led_message.mode, led_message.led_number);
	// 		}else{
	// 			LOG_DBG("No leds chan");
	// 		}
	// }
}

K_THREAD_DEFINE(subscriber_task_id, 512, leds_obs_task, NULL, NULL, NULL, 3, 0, 0);

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

	gopro_led_set_bt(0);
	gopro_led_set_rec(0);
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
