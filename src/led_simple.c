#include "led_simple.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_HAS_LED_SIMPLE

#include "gopro_client.h"

#define LED0_NODE	DT_ALIAS(led0)
#if DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#define LED_REC_PRESENT
#endif

#define LED1_NODE	DT_ALIAS(led1)
#if DT_NODE_HAS_STATUS_OKAY(LED1_NODE)
#define LED_BT_PRESENT
#endif


LOG_MODULE_REGISTER(gopro_ledsimple, CONFIG_LEDS_LOG_LVL);

#ifdef LED_REC_PRESENT
static struct gpio_dt_spec led_rec = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,{0});
#endif

#ifdef LED_BT_PRESENT
static struct gpio_dt_spec led_bt = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios,{0});
#endif

enum led_mode_t leds_mode[LED_NUM_END];

extern const struct led_mode_timing_t led_mode_timing[LED_MODE_END];

static void led_set_bt(uint8_t val);
static void led_set_rec(uint8_t val);

#ifdef LED_BT_PRESENT
K_THREAD_STACK_DEFINE(bt_stack_area, 512);
struct k_thread bt_thread_data;
static void leds_bt_task(void *, void *, void *);
#endif

#ifdef LED_REC_PRESENT
K_THREAD_STACK_DEFINE(rec_stack_area, 512);
struct k_thread rec_thread_data;
static void leds_rec_task(void *, void *, void *);
#endif


#ifdef LED_BT_PRESENT
static void leds_bt_task(void *, void *, void *){
	k_timeout_t on_time, off_time;

	while(1){
		on_time = led_mode_timing[leds_mode[LED_NUM_BT]].on_time;
		off_time = led_mode_timing[leds_mode[LED_NUM_BT]].off_time;
		
		if(on_time.ticks > 0){
			led_set_bt(1);
			k_sleep(on_time);
		}

		if(off_time.ticks > 0){
			led_set_bt(0);
			k_sleep(off_time);
		}
	}
}
#endif

#ifdef LED_REC_PRESENT
static void leds_rec_task(void *, void *, void *){
	k_timeout_t on_time, off_time;

	while(1){
		on_time = led_mode_timing[leds_mode[LED_NUM_REC]].on_time;
		off_time = led_mode_timing[leds_mode[LED_NUM_REC]].off_time;
		
		if(on_time.ticks > 0){
			led_set_rec(1);
			k_sleep(on_time);
		}

		if(off_time.ticks > 0){
			led_set_rec(0);
			k_sleep(off_time);
		}
	}

}
#endif

int led_hw_init(void){
	int err;

	#ifdef LED_REC_PRESENT
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
			LOG_INF("Set up LED at %s pin %d", led_rec.port->name, led_rec.pin);
		}
	}

    k_thread_create(&rec_thread_data, rec_stack_area,
                    K_THREAD_STACK_SIZEOF(rec_stack_area),
                    leds_rec_task,
                    NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);

	#endif

	#ifdef LED_BT_PRESENT
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
			LOG_INF("Set up LED at %s pin %d", led_bt.port->name, led_bt.pin);
		}
	}

    k_thread_create(&bt_thread_data, bt_stack_area,
                    K_THREAD_STACK_SIZEOF(bt_stack_area),
                    leds_bt_task,
                    NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);
	#endif

	return 0;
}

int led_mode_set(enum led_number_t led_num, enum led_mode_t mode){
	
	if(mode >= LED_MODE_END) {
		LOG_ERR("Ivalid mode number: %d of %d",mode,LED_MODE_END-1);
		return -1;
	}

	if(led_num >= LED_NUM_END) {
		LOG_ERR("Ivalid led number: %d of %d",led_num,LED_NUM_END-1);
		return -2;
	}

    switch (led_num)
        {
        case LED_NUM_BT:
            leds_mode[SIMPLE_LED_BT] = mode;
            break;

        case LED_NUM_REC:
            leds_mode[SIMPLE_LED_REC] = mode;
            break;
        
        default:
            LOG_ERR("Led number not found: %d",led_num);
            break;
		}
	
		return 0;
}

static void led_set_bt(uint8_t val){
	if(val){
		gpio_pin_set_dt(&led_bt,1);
	}else{
		gpio_pin_set_dt(&led_bt,0);
	}
}

static void led_set_rec(uint8_t val){
	if(val){
		gpio_pin_set_dt(&led_rec,1);
	}else{
		gpio_pin_set_dt(&led_rec,0);
	}
}



#endif