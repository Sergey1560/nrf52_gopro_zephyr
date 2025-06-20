#include "canbus.h"

#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <gopro_client.h>

#define CAN_MCP_NODE	DT_ALIAS(cannode)

#if DT_NODE_HAS_STATUS_OKAY(CAN_MCP_NODE)
#define CANBUS_PRESENT
#endif

#ifdef CANBUS_PRESENT
LOG_MODULE_REGISTER(canbus_gopro, LOG_LEVEL_DBG);

#if !DT_NODE_EXISTS(DT_NODELABEL(mcp_rst_switch))
#error "Overlay for MCP2515 RST pin not properly defined."
#endif

static void can_tx_timer_handler(struct k_timer *dummy);
static void can_tx_subscriber_task(void *ptr1, void *ptr2, void *ptr3);


ZBUS_CHAN_DEFINE(can_tx_chan,                           	/* Name */
         struct can_frame,                       		      	/* Message type */
         NULL,                                       	/* Validator */
         NULL,                                       	/* User Data */
         ZBUS_OBSERVERS(can_tx_subscriber),  	        		/* observers */
         ZBUS_MSG_INIT(0)       						/* Initial value */
);

ZBUS_MSG_SUBSCRIBER_DEFINE(can_tx_subscriber);
K_THREAD_DEFINE(can_tx_subscriber_task_id, 1024, can_tx_subscriber_task, NULL, NULL, NULL, 3, 0, 0);

ZBUS_CHAN_DECLARE(gopro_cmd_chan);

K_TIMER_DEFINE(can_tx_timer, can_tx_timer_handler, NULL);
#define CAN_TX_TIMER_START	do{k_timer_start(&can_tx_timer, K_MSEC(100), K_MSEC(100));}while(0)

const struct can_timing mcp2515_8mhz_500 = {
	.sjw = 2,
	.prop_seg = 2,
	.phase_seg1 = 3,
	.phase_seg2 = 2,
	.prescaler = 1
};

static const struct gpio_dt_spec mcp_rst_switch = 	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(mcp_rst_switch), gpios, {0});
const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

const struct can_frame err_state_frame = {
		.flags = 0,
		.id = GPCAN_REPLY_MSG_ID,
		.dlc = 1,
		.data = {0xFF,0,0,0,0,0,0,0}
};


#ifdef GPCAN_ENABLE_FILTER
const struct can_filter goprocan_filter = {
        .flags = 0,
        .id = GPCAN_INPUT_CMD_ID,
        .mask = 0x7F8
};
#else
const struct can_filter goprocan_filter = {
        .flags = 0,
        .id = 0,
        .mask = 0
};
#endif


static void rx_callback_function(const struct device *dev, struct can_frame *frame, void *user_data)
{
    int err;
	struct gopro_cmd_t gopro_cmd;    
	LOG_INF("Get CAN frame 0x%0X",frame->id);

	memset(&gopro_cmd,0,sizeof(struct gopro_cmd_t));

	switch (frame->id)
	{
	case GPCAN_INPUT_CMD_ID: //GoPro cmd
		gopro_cmd.cmd_type = 72;
		break;

	case GPCAN_INPUT_SET_ID: //GoPro cmd
		gopro_cmd.cmd_type = 74;
		break;

	case GPCAN_INPUT_QUERY_ID: //GoPro cmd
		gopro_cmd.cmd_type = 76;
		break;

	default:
		return;
		break;
	}


	if((frame->dlc > 0) && (frame->dlc <= GOPRO_CMD_DATA_LEN)){

		gopro_cmd.len = frame->dlc;
		
		for(uint32_t i=0; i<frame->dlc; i++){
			gopro_cmd.data[i] = frame->data[i];
		}

		err = zbus_chan_pub(&gopro_cmd_chan, &gopro_cmd, K_NO_WAIT);
		if(err != 0){
			if(err == -ENOMSG){
				LOG_ERR("Invalid Gopro state, skip cmd");
				
				zbus_chan_pub(&can_tx_chan, &err_state_frame, K_NO_WAIT);
				
				return;
			}
			LOG_ERR("CMD chan pub failed: %d",err);
		}
	}	


}


void mcp2515_get_timing(struct can_timing *timing, uint8_t cnf1, uint8_t cnf2, uint8_t cnf3){
	timing->sjw= (cnf1 >> 6) + 1;
	timing->prescaler= (cnf1 & 0x3F) + 1;

	timing->phase_seg1= ((cnf2 >> 3) & 7)+1;
	timing->prop_seg= (cnf2 & 0x7) + 1;

	timing->phase_seg2=(cnf3 & 7)+1;
}


int canbus_init(void){
    int err;

	if (!gpio_is_ready_dt(&mcp_rst_switch)) {
		LOG_ERR("The MCP2515 RST pin GPIO port is not ready.");
		return -1;
	}

	err = gpio_pin_configure_dt(&mcp_rst_switch, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("Configuring RST pin failed: %d", err);
		return-1;
	}

	err = gpio_pin_set_dt(&mcp_rst_switch, 1);
	if (err != 0) {
		LOG_ERR("Setting RST pin level failed: %d\n", err);
		return -1;
	}

	if (!device_is_ready(can_dev)) {
		LOG_ERR("CAN: Device %s not ready.\n", can_dev->name);
		return -1;
	}else{
		LOG_DBG("CAN device ready");
	}

	err = can_set_mode(can_dev, CAN_MODE_NORMAL);

    if (err != 0) {
        LOG_ERR("Error setting CAN mode [%d]", err);
        return -1;
    }else{
    	LOG_DBG("MODE NORMAL Enabled.");
	}

	// LOG_DBG("Can bitrate MIN: %d MAX: %d",can_get_bitrate_min(can_dev),can_get_bitrate_max(can_dev));
	// mcp2515_get_timing(&timing, 0x40, 0x91, 0x01);
	// const struct can_timing *min = can_get_timing_min(can_dev);
  	// const struct can_timing *max = can_get_timing_max(can_dev);
	// LOG_DBG("SWJ: %d  MIN: %d MAX: %d",timing.sjw,min->sjw,max->sjw);
	// LOG_DBG("Prescaler: %d  MIN: %d MAX: %d",timing.prescaler,min->prescaler,max->prescaler);
	// LOG_DBG("Pseg1: %d  MIN: %d MAX: %d",timing.phase_seg1,min->phase_seg1,max->phase_seg1);
	// LOG_DBG("Pseg2: %d  MIN: %d MAX: %d",timing.phase_seg2,min->phase_seg2,max->phase_seg2);
	// LOG_DBG("Propseg: %d  MIN: %d MAX: %d",timing.prop_seg,min->prop_seg,max->prop_seg);

	err = can_set_timing(can_dev, &mcp2515_8mhz_500);
	
	if (err != 0) {
		LOG_ERR("Failed to set timing: %d",err);
        return err;
    }

	int filter_id = can_add_rx_filter(can_dev, rx_callback_function, NULL, &goprocan_filter);

    if (filter_id < 0) {
		LOG_ERR("Unable to add rx filter [%d]", filter_id);
        return -1;
    }

	err = can_start(can_dev);
	if (err != 0) {
		LOG_ERR("Error starting CAN controller [%d]", err);
		return err;
	}else{
		LOG_INF("CAN Start");
	}

	CAN_TX_TIMER_START;
	
    return 0;
}

static void can_tx_subscriber_task(void *ptr1, void *ptr2, void *ptr3){
	int err;
	struct can_frame tx_frame;
	ARG_UNUSED(ptr1);
	ARG_UNUSED(ptr2);
	ARG_UNUSED(ptr3);
	const struct zbus_channel *chan;

	while (!zbus_sub_wait_msg(&can_tx_subscriber, &chan, &tx_frame, K_FOREVER)) {
		if (&can_tx_chan == chan) {
				//LOG_DBG("Msg to send: 0x%0X len: %d", tx_frame.id,tx_frame.dlc);

				err = can_send(can_dev, &tx_frame, K_MSEC(10), NULL, NULL);

				if (err != 0) {
					LOG_ERR("CAN Sending failed [%d]", err);
				}

			}
	}
};

static void can_tx_timer_handler(struct k_timer *dummy){
	struct can_frame tx_frame;
	memset(&tx_frame,0,sizeof(struct can_frame));

	tx_frame.id = GPCAN_HEART_BEAT_ID;
	tx_frame.dlc = 3;

	tx_frame.data[0] = gopro_client_get_state();
	tx_frame.data[1] = 0;  //rec on/off
	tx_frame.data[2] = 85; //Battery 

	zbus_chan_pub(&can_tx_chan, &tx_frame, K_NO_WAIT);
};



#else

int canbus_init(void){
	return 0;
}

int can_hb(void){
	return 0;
}

#endif