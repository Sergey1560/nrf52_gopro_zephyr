#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/util.h>
#include <zephyr/devicetree.h>

#include "led_rgb.h"

#ifdef CONFIG_HAS_LED_RGB

LOG_MODULE_REGISTER(gopro_ledrgb, CONFIG_LEDS_LOG_LVL);

#define LED_STRIP_NODE		DT_ALIAS(ledstrip)

#if DT_NODE_HAS_STATUS(LED_STRIP_NODE, okay)
    #define STRIP_NUM_PIXELS    DT_PROP(LED_STRIP_NODE, chain_length)
#else
    #error "Led Strip Node is disabled"
#endif

static const struct device *const strip = DEVICE_DT_GET(LED_STRIP_NODE);

#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b) }

const struct led_rgb red_color = RGB(CONFIG_SAMPLE_LED_BRIGHTNESS, 0x00, 0x00);
const struct led_rgb green_color = RGB(0x00, CONFIG_SAMPLE_LED_BRIGHTNESS, 0x00);
const struct led_rgb blue_color = RGB(0x00, 0x00, CONFIG_SAMPLE_LED_BRIGHTNESS);
const struct led_rgb off_color = RGB(0, 0x00, 0x00);

static struct led_rgb pixels[STRIP_NUM_PIXELS];
enum led_mode_t rgb_leds_mode[STRIP_NUM_PIXELS];

extern const struct led_mode_timing_t led_mode_timing[LED_MODE_END];

K_THREAD_STACK_DEFINE(bt_stack_area, 512);
struct k_thread bt_thread_data;
static void rgb_leds_bt_task(void *, void *, void *);

#if STRIP_NUM_PIXELS > 1
K_THREAD_STACK_DEFINE(rec_stack_area, 512);
struct k_thread rec_thread_data;
static void rgb_leds_rec_task(void *, void *, void *);
#endif


int led_hw_init(void){
    int ret;

	if (device_is_ready(strip)) {
		LOG_INF("Found LED strip device %s pixels: %d", strip->name, STRIP_NUM_PIXELS);
	} else {
		LOG_ERR("LED strip device %s is not ready", strip->name);
		return -1;
	}

    memcpy(pixels,(struct led_rgb *)&off_color,sizeof(struct led_rgb)*STRIP_NUM_PIXELS);

    ret = led_strip_update_rgb(strip, (struct led_rgb *)&off_color, STRIP_NUM_PIXELS);
    if (ret) {
        LOG_ERR("couldn't update strip: %d", ret);
    }

    k_thread_create(&bt_thread_data, bt_stack_area,
                    K_THREAD_STACK_SIZEOF(bt_stack_area),
                    rgb_leds_bt_task,
                    NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);

    #if STRIP_NUM_PIXELS > 1
    k_thread_create(&rec_thread_data, rec_stack_area,
                    K_THREAD_STACK_SIZEOF(rec_stack_area),
                    rgb_leds_bt_task,
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
            rgb_leds_mode[RGB_PIXEL_BT] = mode;
            break;

        case LED_NUM_REC:
            #if STRIP_NUM_PIXELS > 1
            rgb_leds_mode[RGB_PIXEL_REC] = mode;
            #endif
            break;
        
        default:
            LOG_ERR("Led number not found: %d",led_num);
            break;
        }


	return 0;
}


static void rgb_leds_bt_task(void *, void *, void *){
	k_timeout_t on_time, off_time;
    int ret;

	while(1){
		on_time = led_mode_timing[rgb_leds_mode[RGB_PIXEL_BT]].on_time;
		off_time = led_mode_timing[rgb_leds_mode[RGB_PIXEL_BT]].off_time;
		
		if(on_time.ticks > 0){
            memcpy(&pixels[RGB_PIXEL_BT],(struct led_rgb *)&green_color,sizeof(struct led_rgb));
            ret = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
                if (ret) {
                    LOG_ERR("couldn't update strip: %d", ret);
                }
            k_sleep(on_time);
		}

		if(off_time.ticks > 0){
            memcpy(&pixels[RGB_PIXEL_BT],(struct led_rgb *)&off_color,sizeof(struct led_rgb));
            ret = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
                if (ret) {
                    LOG_ERR("couldn't update strip: %d", ret);
                }
			k_sleep(off_time);
		}
	}
}

#if STRIP_NUM_PIXELS > 1
static void rgb_leds_rec_task(void *, void *, void *){
	k_timeout_t on_time, off_time;
    int ret;

	while(1){
		on_time = led_mode_timing[rgb_leds_mode[RGB_PIXEL_REC]].on_time;
		off_time = led_mode_timing[rgb_leds_mode[RGB_PIXEL_REC]].off_time;
		
		if(on_time.ticks > 0){
            memcpy(&pixels[RGB_PIXEL_REC],(struct led_rgb *)&red_color,sizeof(struct led_rgb));
            ret = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
                if (ret) {
                    LOG_ERR("couldn't update strip: %d", ret);
                }
			k_sleep(on_time);
		}

		if(off_time.ticks > 0){
            memcpy(&pixels[RGB_PIXEL_REC],(struct led_rgb *)&off_color,sizeof(struct led_rgb));
            ret = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
                if (ret) {
                    LOG_ERR("couldn't update strip: %d", ret);
                }
			k_sleep(off_time);
		}
	}

}
#endif

#endif