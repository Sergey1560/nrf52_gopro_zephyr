#include "buttons.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#define SW0_NODE	DT_ALIAS(sw0)
#define SW1_NODE	DT_ALIAS(sw1)

#if DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#define BUTTON_PRESENT
#endif

#ifdef BUTTON_PRESENT
static const struct gpio_dt_spec button_rec = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,{0});
static const struct gpio_dt_spec button_hl  = GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios,{0});

static struct gpio_callback button_cb_data;


LOG_MODULE_REGISTER(gopro_buttons, LOG_LEVEL_DBG);


void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
	static uint8_t cmd_index = 0;

	LOG_INF("Button pressed at %" PRIu32 " pins: %d", k_cycle_get_32(),pins);
	
	// if(pins & BIT(button_rec.pin)){
	// 	LOG_INF("REC Button");
	// 	if (led.port) {
	// 			gpio_pin_toggle_dt(&led);
	// 	}

	// 	k_fifo_put(&fifo_uart_rx_data, &cmd_buf_list[cmd_index]);
	// 	cmd_index++;

	// 	if(cmd_index > (sizeof(cmd_buf_list)/sizeof(cmd_buf_list[0]))-1 ){
	// 		cmd_index = 0;
	// 	}
	// }
	
	// if(pins & BIT(button_hl.pin)){
	// 	LOG_INF("HL Button");

	// 	k_fifo_put(&fifo_uart_rx_data, &cmd_hl);
	// }
}

void gopro_gpio_init(void){
	int err=0;

	if (!gpio_is_ready_dt(&button_rec)) {
		LOG_ERR("Error: button device %s is not ready\n",
		       button_rec.port->name);
		return;
	}

	if (!gpio_is_ready_dt(&button_hl)) {
		LOG_ERR("Error: button device %s is not ready\n",
		       button_hl.port->name);
		return;
	}


	err = gpio_pin_configure_dt(&button_rec, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       err, button_rec.port->name, button_rec.pin);
		return;
	}


	err = gpio_pin_configure_dt(&button_hl, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       err, button_hl.port->name, button_hl.pin);
		return;
	}

	err = gpio_pin_interrupt_configure_dt(&button_rec, GPIO_INT_EDGE_TO_ACTIVE);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d\n", err, button_rec.port->name, button_rec.pin);
		return;
	}

	err = gpio_pin_interrupt_configure_dt(&button_hl, GPIO_INT_EDGE_TO_ACTIVE);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d\n", err, button_hl.port->name, button_hl.pin);
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button_rec.pin)|BIT(button_hl.pin));
	gpio_add_callback(button_rec.port, &button_cb_data);

	LOG_INF("Set up REC button at %s pin %d\n", button_rec.port->name, button_rec.pin);

}

#else
void gopro_gpio_init(void){
    
};


#endif


