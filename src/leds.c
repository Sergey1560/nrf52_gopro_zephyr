#include "leds.h"
#include "led_simple.h"
#include "led_rgb.h"
#include "gopro_client.h"

LOG_MODULE_REGISTER(gopro_leds, CONFIG_LEDS_LOG_LVL);

static void led_idle_handler(struct k_work *work);
static void led_idle_timer_handler(struct k_timer *dummy);

K_WORK_DEFINE(led_idle_work, led_idle_handler);
K_TIMER_DEFINE(led_idle_timer, led_idle_timer_handler, NULL);

const struct led_mode_timing_t led_mode_timing[LED_MODE_END] = {
	{.on_time=K_MSEC(0),   .off_time=K_MSEC(100)},		//LED_MODE_OFF
	{.on_time=K_MSEC(100), .off_time=K_MSEC(900)},		//LED_MODE_BLINK_1S
	{.on_time=K_MSEC(100), .off_time=K_MSEC(4900)},		//LED_MODE_BLINK_5S
	{.on_time=K_MSEC(100), .off_time=K_MSEC(200)},		//LED_MODE_BLINK_300MS
	{.on_time=K_MSEC(100), .off_time=K_MSEC(100)},		//LED_MODE_BLINK_100MS
	{.on_time=K_MSEC(100), .off_time=K_MSEC(0)}			//LED_MODE_ON
};


int gopro_leds_init(void){

    #if defined(CONFIG_HAS_LED_SIMPLE) || defined(CONFIG_HAS_LED_RGB)
    led_hw_init();
    #endif

    gopro_led_mode_set(LED_NUM_BT,LED_MODE_BLINK_5S);
    return 0;
}
int gopro_led_mode_set(enum led_number_t led_num, enum led_mode_t mode){

    LOG_DBG("LED set num %d mode %d",led_num,mode);
    #if defined(CONFIG_HAS_LED_SIMPLE) || defined(CONFIG_HAS_LED_RGB)
    led_mode_set(led_num,mode);
    #endif

    return 0;
}

void led_idle_timer_start(uint8_t enable){

	if(enable == 1){
		k_timer_start(&led_idle_timer, K_SECONDS(5), K_SECONDS(5));
	}else if(enable == 0){
		k_timer_stop(&led_idle_timer);	
	}
	
}

static void led_idle_handler(struct k_work *work){
	if(gopro_client_get_state() != GP_STATE_UNKNOWN){
		LOG_DBG("Set LED to idle state");
		gopro_led_mode_set(LED_NUM_BT,LED_MODE_BLINK_5S);
		gopro_client_set_sate(GP_STATE_UNKNOWN);
		gopro_client_setname(NULL,0);
	}
}

static void led_idle_timer_handler(struct k_timer *dummy){
    k_work_submit(&led_idle_work);
}
