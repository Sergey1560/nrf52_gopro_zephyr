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
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>

#include "gopro_client.h"

//#define DISCOVERY_TIMEOUT   K_MSEC(5000)
#define DISCOVERY_TIMEOUT   K_FOREVER
#define BLE_WRITE_TIMEOUT	K_MSEC(1200)

int gopro_bt_start(void);
void gopro_start_discovery(struct bt_conn *conn, struct bt_gopro_client *gopro_client);

#endif