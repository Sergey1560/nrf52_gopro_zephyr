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

#include "gopro_client.h"

#define LED0_NODE	DT_ALIAS(led0)
#if DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#define LED_PRESENT
#endif


#ifdef LED_PRESENT
LOG_MODULE_REGISTER(gopro_leds, LOG_LEVEL_DBG);

static struct gpio_dt_spec led_rec = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,{0});
static struct gpio_dt_spec led_bt = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios,{0});

enum led_mode_t leds_mode[LED_NUM_END];

const struct led_mode_timing_t led_mode_timing[LED_MODE_END] = {
	{.on_time=K_MSEC(0),   .off_time=K_MSEC(100)},		//LED_MODE_OFF
	{.on_time=K_MSEC(100), .off_time=K_MSEC(900)},		//LED_MODE_BLINK_1S
	{.on_time=K_MSEC(100), .off_time=K_MSEC(4900)},		//LED_MODE_BLINK_5S
	{.on_time=K_MSEC(100), .off_time=K_MSEC(200)},		//LED_MODE_BLINK_300MS
	{.on_time=K_MSEC(100), .off_time=K_MSEC(100)},		//LED_MODE_BLINK_100MS
	{.on_time=K_MSEC(100), .off_time=K_MSEC(0)}			//LED_MODE_ON
};


static void leds_bt_task(void);
static void leds_rec_task(void);
static void leds_callback(const struct zbus_channel *chan);

static void gopro_led_set_bt(uint8_t val);
static void gopro_led_set_rec(uint8_t val);

static void led_idle_handler(struct k_work *work);
static void led_idle_timer_handler(struct k_timer *dummy);

ZBUS_CHAN_DEFINE(leds_chan,                           	/* Name */
         struct led_message_t,                       		      	/* Message type */
         NULL,                                       	/* Validator */
         NULL,                                       	/* User Data */
         ZBUS_OBSERVERS(leds_listener),  	        		/* observers */
         ZBUS_MSG_INIT(0)       						/* Initial value */
);

ZBUS_LISTENER_DEFINE(leds_listener, leds_callback);

K_THREAD_DEFINE(led_bt_task_id, 512, leds_bt_task, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(led_rec_task_id, 512, leds_rec_task, NULL, NULL, NULL, 7, 0, 0);

K_WORK_DEFINE(led_idle_work, led_idle_handler);
K_TIMER_DEFINE(led_idle_timer, led_idle_timer_handler, NULL);
//#define LED_TIMER_START	do{k_timer_start(&led_idle_timer, K_SECONDS(5), K_SECONDS(5));}while(0)

static void leds_callback(const struct zbus_channel *chan)
{
 	const struct led_message_t *led_message;
	if (&leds_chan == chan) {
			led_message = zbus_chan_const_msg(chan); // Direct message access
			LOG_DBG("Get led message");

			switch (led_message->led_number)
			{
			case LED_NUM_BT:
				leds_mode[LED_NUM_BT] = led_message->mode;
				break;

			case LED_NUM_REC:
				leds_mode[LED_NUM_REC] = led_message->mode;
				break;
			
			default:
				LOG_ERR("Led number not found: %d",led_message->led_number);
				break;
			}
	}
}

static void leds_bt_task(void){
	k_timeout_t on_time, off_time;

	while(1){
		on_time = led_mode_timing[leds_mode[LED_NUM_BT]].on_time;
		off_time = led_mode_timing[leds_mode[LED_NUM_BT]].off_time;
		
		if(on_time.ticks > 0){
			gopro_led_set_bt(1);
			k_sleep(on_time);
		}

		if(off_time.ticks > 0){
			gopro_led_set_bt(0);
			k_sleep(off_time);
		}
	}
}

static void leds_rec_task(void){
	k_timeout_t on_time, off_time;

	while(1){
		on_time = led_mode_timing[leds_mode[LED_NUM_REC]].on_time;
		off_time = led_mode_timing[leds_mode[LED_NUM_REC]].off_time;
		
		if(on_time.ticks > 0){
			gopro_led_set_rec(1);
			k_sleep(on_time);
		}

		if(off_time.ticks > 0){
			gopro_led_set_rec(0);
			k_sleep(off_time);
		}
	}

}

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

	gopro_led_mode_set(LED_NUM_BT,LED_MODE_BLINK_5S);

	return 0;
}

int gopro_led_mode_set(enum led_number_t led_num, enum led_mode_t mode){
	struct led_message_t led_message;
	
	if(mode >= LED_MODE_END) {
		LOG_ERR("Ivalid mode number: %d of %d",mode,LED_MODE_END-1);
		return -1;
	}

	if(led_num >= LED_NUM_END) {
		LOG_ERR("Ivalid led number: %d of %d",led_num,LED_NUM_END-1);
		return -2;
	}

	led_message.mode = mode;
	led_message.led_number = led_num;
	zbus_chan_pub(&leds_chan, &led_message, K_NO_WAIT);			

	return 0;
}

static void gopro_led_set_bt(uint8_t val){
	if(val){
		gpio_pin_set_dt(&led_bt,1);
	}else{
		gpio_pin_set_dt(&led_bt,0);
	}
}

static void gopro_led_set_rec(uint8_t val){
	if(val){
		gpio_pin_set_dt(&led_rec,1);
	}else{
		gpio_pin_set_dt(&led_rec,0);
	}
}

static void led_idle_handler(struct k_work *work){
	if(gopro_client_get_state() != GPSTATE_UNKNOWN){
		LOG_DBG("Set LED to idle state");
		gopro_led_mode_set(LED_NUM_BT,LED_MODE_BLINK_5S);
		gopro_client_set_sate(GPSTATE_UNKNOWN);
		gopro_client_setname(NULL,0);
	}
}

static void led_idle_timer_handler(struct k_timer *dummy){
    k_work_submit(&led_idle_work);
}

void led_idle_timer_start(uint8_t enable){

	if(enable == 1){
		k_timer_start(&led_idle_timer, K_SECONDS(5), K_SECONDS(5));
	}else if(enable == 0){
		k_timer_stop(&led_idle_timer);	
	}
	
}


#else
int gopro_leds_init(void){
    return 0;
}
int gopro_led_mode_set(enum led_mode_t mode, enum led_number_t led_num){
	return 0;
}
#endif

