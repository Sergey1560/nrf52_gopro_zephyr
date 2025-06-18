/** @file
 *  @brief Gopro control sample
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/zbus/zbus.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>

#include <gopro_client.h>
#include <canbus.h>
#include <buttons.h>
#include <leds.h>

LOG_MODULE_REGISTER(central_gopro, LOG_LEVEL_DBG);

ZBUS_CHAN_DECLARE(leds_chan);

/* payload buffer element size. */
#define DATA_BUF_SIZE 20

#define GOPRO_WRITE_TIMEOUT K_MSEC(150)

static struct k_work scan_work;

K_SEM_DEFINE(gopro_write_sem, 0, 1);

static struct bt_scan_device_info *gopro_device_info;

struct write_data_t {
	void *fifo_reserved;
	uint16_t len;
	uint8_t  data[DATA_BUF_SIZE];
};

static K_FIFO_DEFINE(fifo_uart_rx_data);

static struct bt_conn *default_conn;
static struct bt_gopro_client gopro_client;

static struct write_data_t cmd_buf_list[] = {
	{.len = 4, .data={3,1,1,1}},  //shutter on
	{.len = 4, .data={3,1,1,0}}	  //shutter off
}; 

static struct write_data_t cmd_hl = {.len=2, .data={1,0x18}};

static void led_idle_handler(struct k_work *work);
static void led_idle_timer_handler(struct k_timer *dummy);

K_WORK_DEFINE(led_idle_work, led_idle_handler);
K_TIMER_DEFINE(led_idle_timer, led_idle_timer_handler, NULL);
#define LED_TIMER_START	do{k_timer_start(&led_idle_timer, K_SECONDS(5), K_SECONDS(5));}while(0)

static void ble_data_sent(struct bt_gopro_client *nus, uint8_t err, const uint8_t *const data, uint16_t len)
{
	ARG_UNUSED(nus);
	ARG_UNUSED(data);
	ARG_UNUSED(len);

	k_sem_give(&gopro_write_sem);

	if (err) {
		LOG_WRN("ATT error code: 0x%02X", err);
	}
}

static uint8_t ble_data_received(struct bt_gopro_client *nus,	const uint8_t *data, uint16_t len)
{
	ARG_UNUSED(nus);

	LOG_HEXDUMP_DBG(data,len,"Recieve data:");

	return BT_GATT_ITER_CONTINUE;
}


static void discovery_complete(struct bt_gatt_dm *dm, void *context)
{
	struct bt_gopro_client *nus = context;
	LOG_INF("Service discovery completed");

	bt_security_t sec_level_str = bt_conn_get_security(default_conn);

	LOG_INF("Security Level now: %d",sec_level_str);

	bt_gatt_dm_data_print(dm);

	LOG_INF("Assign handles");
	bt_gopro_handles_assign(dm, nus);
	LOG_INF("Subscribe");
	bt_gopro_subscribe_receive(nus);
	LOG_INF("Release data");
	bt_gatt_dm_data_release(dm);

	gopro_led_mode_set(LED_NUM_BT,LED_MODE_ON);
	gopro_client_set_sate(GPSTATE_CONNECTED);

}

static void discovery_service_not_found(struct bt_conn *conn, void *context)
{
	LOG_WRN("Service not found");
}

static void discovery_error(struct bt_conn *conn, int err,void *context)
{
	LOG_WRN("Error while discovering GATT database: (%d)", err);
}

struct bt_gatt_dm_cb discovery_cb = {
	.completed         = discovery_complete,
	.service_not_found = discovery_service_not_found,
	.error_found       = discovery_error,
};

static void gatt_discover(struct bt_conn *conn)
{
	int err;

	LOG_INF("Start discovery");

	if (conn != default_conn) {
		return;
	}

	err = bt_gatt_dm_start(conn, BT_UUID_GOPRO_SERVICE, &discovery_cb, &gopro_client);
	if (err) {
		LOG_ERR("could not start the discovery procedure, error code: %d", err);
	}
}

static void exchange_func(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
	if (!err) {
		LOG_INF("MTU exchange done");
	} else {
		LOG_WRN("MTU exchange failed (err %" PRIu8 ")", err);
	}
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		LOG_INF("Failed to connect to %s, 0x%02x %s", addr, conn_err, bt_hci_err_to_str(conn_err));

		if (default_conn == conn) {
			bt_conn_unref(default_conn);
			default_conn = NULL;

			(void)k_work_submit(&scan_work);
		}

		return;
	}

	LOG_INF("Connected: %s", addr);
	
	k_timer_stop(&led_idle_timer);

	static struct bt_gatt_exchange_params exchange_params;

	exchange_params.func = exchange_func;
	err = bt_gatt_exchange_mtu(conn, &exchange_params);
	if (err) {
		LOG_WRN("MTU exchange failed (err %d)", err);
	}

	LOG_INF("Change security");
	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		LOG_WRN("Failed to set security: %d", err);
		gatt_discover(conn);
	}

	err = bt_scan_stop();
	if ((!err) && (err != -EALREADY)) {
		LOG_ERR("Stop LE scan failed (err %d)", err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s, reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));

	gopro_led_mode_set(LED_NUM_BT,LED_MODE_BLINK_5S);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;

	(void)k_work_submit(&scan_work);
}

static void security_changed(struct bt_conn *conn, bt_security_t level,enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u", addr, level);
	} else {
		LOG_WRN("Security failed: %s level %u err %d %s", addr, level, err,
			bt_security_err_to_str(err));
	}

	gatt_discover(conn);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed
};


static bool eir_found(struct bt_data *data, void *user_data)
{
	int err;
	struct bt_conn_le_create_param *conn_params;


	if(data->type == 9){

		gopro_client_setname((char *)data->data,data->data_len);

	}else if((data->type == 255) && (data->data_len == 14) ){
		
		switch (data->data[3])
		{
		case 0:
			gopro_led_mode_set(LED_NUM_BT,LED_MODE_BLINK_1S);
			gopro_client_set_sate(GPSTATE_OFFLINE);
			break;
		
		case 1:
			gopro_led_mode_set(LED_NUM_BT,LED_MODE_BLINK_300MS);
			gopro_client_set_sate(GPSTATE_ONLINE);
			LOG_DBG("Camera ON, connecting");
			
			// err = bt_scan_stop();
			// if (err) {
			// 	LOG_ERR("Failed to stop scanning (err %d)", err);
			// 	return err;
			// }

			// conn_params = BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_CODED | BT_CONN_LE_OPT_NO_1M,BT_GAP_SCAN_FAST_INTERVAL,BT_GAP_SCAN_FAST_INTERVAL);
			// gopro_device_info=gopro_client_get_device_info();
			// err = bt_conn_le_create(gopro_device_info->recv_info->addr, conn_params,BT_LE_CONN_PARAM_DEFAULT,&default_conn);

			// if(err != 0){
			// 	LOG_ERR("Conn failed, err: %d",err);
			// }

			break;

		case 5:
			gopro_led_mode_set(LED_NUM_BT,LED_MODE_BLINK_100MS);
			gopro_client_set_sate(GPSTATE_PAIRING);
			LOG_DBG("Camera Pairing");

			// conn_params = BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_CODED | BT_CONN_LE_OPT_NO_1M,BT_GAP_SCAN_FAST_INTERVAL,BT_GAP_SCAN_FAST_INTERVAL);
			// gopro_device_info=gopro_client_get_device_info();
			// err = bt_conn_le_create(gopro_device_info->recv_info->addr, conn_params,BT_LE_CONN_PARAM_DEFAULT,&default_conn);

			// if(err != 0){
			// 	LOG_ERR("Conn failed, err: %d",err);
			// }

			break;

		default:
			LOG_DBG("Camera state: %d",data->data[3]);
			break;
		}
	}else{
		//LOG_DBG("[AD]: type %u data_len %u", data->type, data->data_len);
		//LOG_HEXDUMP_DBG(data->data,data->data_len,"AD data");
	}
	
	return true;
};

static void scan_filter_match(struct bt_scan_device_info *device_info,struct bt_scan_filter_match *filter_match, bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	LOG_DBG("Filters matched. Address: %s connectable: %d", addr, connectable);

	gopro_client_set_device_info(device_info);

	LED_TIMER_START;

	bt_data_parse(device_info->adv_data,eir_found,(void *)device_info->recv_info->addr);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	LOG_WRN("Connecting failed");
}

static void scan_connecting(struct bt_scan_device_info *device_info, struct bt_conn *conn)
{
	LOG_DBG("Scan connecting");
	default_conn = bt_conn_ref(conn);
}

static int gopro_client_init(void){
	int err;
	struct bt_gopro_client_init_param init = {
		.cb = {
			.received = ble_data_received,
			.sent = ble_data_sent,
		}
	};

	err = bt_gopro_client_init(&gopro_client, &init);
	if (err) {
		LOG_ERR("GoPro Client initialization failed (err %d)", err);
		return err;
	}

	LOG_INF("GoPro Client module initialized");
	return err;
}


static void scan_filter_no_match(struct bt_scan_device_info *device_info, bool connectable){
	char addr[BT_ADDR_LE_STR_LEN];
	
	bt_addr_le_to_str(device_info->recv_info->addr, addr,  sizeof(addr));
	//LOG_DBG("Scan no match %s",addr);
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, scan_filter_no_match, scan_connecting_error, scan_connecting);

static void try_add_address_filter(const struct bt_bond_info *info, void *user_data)
{
	int err;
	char addr[BT_ADDR_LE_STR_LEN];
	uint8_t *filter_mode = user_data;

	bt_addr_le_to_str(&info->addr, addr, sizeof(addr));

	struct bt_conn *conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &info->addr);

	if (conn) {
		bt_conn_unref(conn);
		return;
	}

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_ADDR, &info->addr);
	if (err) {
		LOG_ERR("Address filter cannot be added (err %d): %s", err, addr);
		return;
	}

	LOG_INF("Address filter added: %s", addr);
	*filter_mode |= BT_SCAN_ADDR_FILTER;
}

static int scan_start(void)
{
	int err;
	uint8_t filter_mode = 0;

	err = bt_scan_stop();
	if (err) {
		LOG_ERR("Failed to stop scanning (err %d)", err);
		return err;
	}

	bt_scan_filter_remove_all();

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_GOPRO_SERVICE);
	if (err) {
		LOG_ERR("UUID filter cannot be added (err %d", err);
		return err;
	}
	filter_mode |= BT_SCAN_UUID_FILTER;

	bt_foreach_bond(BT_ID_DEFAULT, try_add_address_filter, &filter_mode);

	err = bt_scan_filter_enable(filter_mode, false);
	if (err) {
		LOG_ERR("Filters cannot be turned on (err %d)", err);
		return err;
	}

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return err;
	}

	LED_TIMER_START;
	LOG_INF("Scan started");
	return 0;
}

static void led_idle_handler(struct k_work *work){
	LOG_DBG("Set LED to idle state");
	gopro_led_mode_set(LED_NUM_BT,LED_MODE_BLINK_5S);
	gopro_client_set_sate(GPSTATE_UNKNOWN);
	gopro_client_setname(NULL,0);
}


static void led_idle_timer_handler(struct k_timer *dummy){
    k_work_submit(&led_idle_work);
}


static void scan_work_handler(struct k_work *item)
{
	ARG_UNUSED(item);
	LOG_DBG("Scan start");
	(void)scan_start();
}

static void scan_init(void)
{
	struct bt_scan_init_param scan_init = {
		.connect_if_match = true,
	};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	k_work_init(&scan_work, scan_work_handler);
	LOG_INF("Scan module initialized");
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s", addr);
}


static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
}


static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_WRN("Pairing failed conn: %s, reason %d %s", addr, reason,
		bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};


int main(void)
{
	int err=0;

	gopro_gpio_init();
	gopro_leds_init();	
	canbus_init();

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		LOG_ERR("Failed to register authorization callbacks.");
		return 0;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		LOG_ERR("Failed to register authorization info callbacks.\n");
		return 0;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}
	LOG_INF("Bluetooth initialized");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = gopro_client_init();
	if (err != 0) {
		LOG_ERR("gopro_client_init failed (err %d)", err);
		return 0;
	}

	scan_init();
	err = scan_start();
	if (err) {
		return 0;
	}


//	bt_unpair(BT_ID_DEFAULT,BT_ADDR_LE_ANY);
	LOG_INF("Starting Bluetooth Central");

	for (;;) {
		/* Wait indefinitely for data to be sent over Bluetooth */
		struct write_data_t *buf = k_fifo_get(&fifo_uart_rx_data,K_FOREVER);

		LOG_HEXDUMP_INF(buf->data, buf->len,"GET Data to send:");

		err = bt_gopro_client_send(&gopro_client, buf->data, buf->len);

		if (err) {
			LOG_WRN("Failed to send data over BLE connection (err %d)", err);
		}

		err = k_sem_take(&gopro_write_sem, GOPRO_WRITE_TIMEOUT);
		if (err) {
			LOG_WRN("Data send timeout");
		}
	}
}
