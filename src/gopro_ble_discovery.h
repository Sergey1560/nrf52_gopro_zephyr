#ifndef GOPRO_BLE_DISCOVERY
#define GOPRO_BLE_DISCOVERY

#include <errno.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#ifdef CONFIG_SOC_SERIES_NRF52X
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>
#else
#include "nrf_hal/gatt_dm.h"
#include "nrf_hal/scan.h"
#endif

#include "gopro_client.h"

//#define DISCOVERY_TIMEOUT   K_MSEC(5000)
#define DISCOVERY_TIMEOUT   K_FOREVER
#define BLE_WRITE_TIMEOUT	K_MSEC(1200)
#define GET_HW_POLL_COUNT   20

int gopro_bt_start(void);
void gopro_start_discovery(struct bt_conn *conn, struct bt_gopro_client *gopro_client);

#endif