#include "buttons.h"
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#define SW0_NODE	DT_ALIAS(sw0)

#if DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#define BUTTON_PRESENT
#endif


#ifdef BUTTON_PRESENT
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include "gopro_client.h"



static const struct gpio_dt_spec button_rec = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,{0});
static struct gpio_callback button_cb_data;

static const struct gopro_cmd_t  gopro_start_rec_msg = {
    	.cmd_type = GP_CNTRL_HANDLE_CMD,
		.len = 4,
        .data = {0x03,0x01,0x01,0x01}
};

static const struct gopro_cmd_t gopro_stop_rec_msg = {
    	.cmd_type = GP_CNTRL_HANDLE_CMD,
        .len = 4,
        .data = {0x03,0x01,0x01,0x00}
};

extern struct gopro_state_t gopro_state;

ZBUS_CHAN_DECLARE(gopro_cmd_chan);

LOG_MODULE_REGISTER(gopro_buttons, LOG_LEVEL_DBG);

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
	int err;
	// static uint8_t cmd_index = 0;
	struct gopro_cmd_t *p_gopro_cmd;

	LOG_INF("Button pressed at %" PRIu32 " pins: %d", k_cycle_get_32(),pins);

	if(gopro_state.record == 1){
		p_gopro_cmd = &gopro_start_rec_msg;
	}else{
		p_gopro_cmd = &gopro_stop_rec_msg;
	}

	err = zbus_chan_pub(&gopro_cmd_chan, p_gopro_cmd, K_NO_WAIT);
	if(err != 0){
	if(err == -ENOMSG){
		LOG_ERR("Invalid Gopro state, skip cmd");
		return;
	}
	LOG_ERR("CMD chan pub failed: %d",err);
	}


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

	LOG_INF("Buttons init");

	if (!gpio_is_ready_dt(&button_rec)) {
		LOG_ERR("Error: button device %s is not ready\n",
		       button_rec.port->name);
		return;
	}

	err = gpio_pin_configure_dt(&button_rec, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       err, button_rec.port->name, button_rec.pin);
		return;
	}

	err = gpio_pin_interrupt_configure_dt(&button_rec, GPIO_INT_EDGE_TO_ACTIVE);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d\n", err, button_rec.port->name, button_rec.pin);
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button_rec.pin));
	gpio_add_callback(button_rec.port, &button_cb_data);

	LOG_INF("Set up REC button at %s pin %d\n", button_rec.port->name, button_rec.pin);

}

#else
void gopro_gpio_init(void){
    
};


#endif


