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


#define CAN_MCP_NODE	DT_ALIAS(cannode)

#if DT_NODE_HAS_STATUS_OKAY(CAN_MCP_NODE)
#define CANBUS_PRESENT
#endif

#ifdef CANBUS_PRESENT

LOG_MODULE_REGISTER(canbus_gopro, LOG_LEVEL_DBG);

const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

const struct can_frame heart_beat_frame = {
			.flags = 0,
			.id = GPCAN_HEART_BEAT_ID,
			.dlc = 8,
			.data = {1,2,3,4,5,6,7,8}
	};

#ifdef GPCAN_ENABLE_FILTER
const struct can_filter goprocan_filter = {
        .flags = 0,
        .id = GPCAN_INPUT_MSG_ID,
        .mask = CAN_STD_ID_MASK
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
        LOG_INF("Get CAN frame 0x%0X",frame->id);
}


static void mcp2515_get_timing(struct can_timing *timing){

	uint8_t cnf1 = 0x40;
	uint8_t cnf2 = 0x91;
	uint8_t cnf3 = 0x01;

	timing->sjw= (cnf1 >> 6) + 1;
	timing->prescaler= (cnf1 & 0x3F) + 1;

	timing->phase_seg1= ((cnf2 >> 3) & 7)+1;
	timing->prop_seg= (cnf2 & 0x7) + 1;

	timing->phase_seg2=(cnf3 & 7)+1;

}


int canbus_init(void){
    int err;
	struct can_timing timing;

    //RST pin
    const struct device *gpio_dev = device_get_binding("GPIO_0");
    err = gpio_pin_configure(gpio_dev, 30, GPIO_OUTPUT);
	
	if(err != 0){
		LOG_ERR("Pin cfg err %d",err);
        return err;
	}
	
	err=gpio_pin_set(gpio_dev,30,1);
		
	if(err != 0){
		LOG_ERR("Pin set err %d",err);
        return err;
    }
	
	if (!device_is_ready(can_dev)) {
		LOG_ERR("CAN: Device %s not ready.\n", can_dev->name);
		return -1;
	}else{
		LOG_INF("CAN device ready");
	}

	err = can_set_mode(can_dev, CAN_MODE_NORMAL);

    if (err != 0) {
        LOG_ERR("Error setting CAN mode [%d]", err);
        return -1;
    }else{
    	LOG_INF("MODE NORMAL Enabled.");
	}

	LOG_DBG("Can bitrate MIN: %d MAX: %d",can_get_bitrate_min(can_dev),can_get_bitrate_max(can_dev));

	mcp2515_get_timing(&timing);

	const struct can_timing *min = can_get_timing_min(can_dev);
  	const struct can_timing *max = can_get_timing_max(can_dev);

	LOG_DBG("SWJ: %d  MIN: %d MAX: %d",timing.sjw,min->sjw,max->sjw);
	LOG_DBG("Prescaler: %d  MIN: %d MAX: %d",timing.prescaler,min->prescaler,max->prescaler);
	LOG_DBG("Pseg1: %d  MIN: %d MAX: %d",timing.phase_seg1,min->phase_seg1,max->phase_seg1);
	LOG_DBG("Pseg2: %d  MIN: %d MAX: %d",timing.phase_seg2,min->phase_seg2,max->phase_seg2);
	LOG_DBG("Propseg: %d  MIN: %d MAX: %d",timing.prop_seg,min->prop_seg,max->prop_seg);

	err = can_set_timing(can_dev, &timing);
	
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

    return 0;
}

int can_hb(void){
    int err;

    err = can_send(can_dev, &heart_beat_frame, K_MSEC(10), NULL, NULL);

    if (err != 0) {
        LOG_ERR("CAN Sending failed [%d]", err);
    }else{
        LOG_INF("Send message done");
    }

    return err;
}

#else

int canbus_init(void){
	return 0;
}

int can_hb(void){
	return 0;
}

#endif