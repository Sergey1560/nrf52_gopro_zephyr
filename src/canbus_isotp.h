#ifndef CAN_ISOTP_H
#define CAN_ISOTP_H

#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/canbus/isotp.h>


#define ISOTP_RX_THREAD_PRIORITY 	9
#define ISOTP_RX_THREAD_STACK_SIZE	2048

#define ISOTP_TX_THREAD_PRIORITY 	9
#define ISOTP_TX_THREAD_STACK_SIZE	2560

void canbus_isotp_init(const struct device *can_dev);

#endif