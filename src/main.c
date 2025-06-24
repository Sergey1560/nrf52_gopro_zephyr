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
static struct k_work scan_work;

//#define BT_AUTO_CONNECT
#ifndef BT_AUTO_CONNECT
struct bt_conn_le_create_param *conn_params = BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_CODED | BT_CONN_LE_OPT_NO_1M,BT_GAP_SCAN_FAST_INTERVAL,BT_GAP_SCAN_FAST_INTERVAL);
#endif

static struct bt_conn *default_conn;
static struct bt_gopro_client gopro_client;

const static struct gopro_cmd_t gopro_query_encoding = {
	.len = 3,
	.cmd_type = GP_HANDLE_QUERY,
	.data={2,GOPRO_QUERY_STATUS_GET_STATUS,GOPRO_STATUS_ID_ENCODING}
};

const static struct gopro_cmd_t gopro_query_battery = {
	.len = 3,
	.cmd_type = GP_HANDLE_QUERY,
	.data={2,GOPRO_QUERY_STATUS_GET_STATUS,GOPRO_STATUS_ID_BAT_PERCENT}
};

const static struct gopro_cmd_t gopro_query_video_num = {
	.len = 3,
	.cmd_type = GP_HANDLE_QUERY,
	.data={2,GOPRO_QUERY_STATUS_GET_STATUS,GOPRO_STATUS_ID_VIDEO_NUM}
};

const static struct gopro_cmd_t gopro_query_register = {
	.len = 5,
	.cmd_type = GP_HANDLE_QUERY,
	.data={4,GOPRO_QUERY_STATUS_REG_STATUS,GOPRO_STATUS_ID_ENCODING,GOPRO_STATUS_ID_VIDEO_NUM,GOPRO_STATUS_ID_BAT_PERCENT}
};

K_MSGQ_DEFINE(gopro_query_q, sizeof(struct gopro_cmd_t *), 10, 1);

const static struct gopro_cmd_t *startup_query_list[] = {&gopro_query_register, &gopro_query_video_num, &gopro_query_battery, &gopro_query_encoding};

static void led_idle_handler(struct k_work *work);
static void led_idle_timer_handler(struct k_timer *dummy);
static void gopro_cmd_subscriber_task(void *ptr1, void *ptr2, void *ptr3);
static void gopro_query_subscriber_task(void *ptr1, void *ptr2, void *ptr3);

bool gopro_cmd_validator(const void* msg, size_t msg_size);

K_WORK_DEFINE(led_idle_work, led_idle_handler);
K_TIMER_DEFINE(led_idle_timer, led_idle_timer_handler, NULL);
#define LED_TIMER_START	do{k_timer_start(&led_idle_timer, K_SECONDS(5), K_SECONDS(5));}while(0)

ZBUS_CHAN_DEFINE(gopro_cmd_chan,                        /* Name */
         struct gopro_cmd_t,                       		/* Message type */
         gopro_cmd_validator,                           /* Validator */
         NULL,                                       	/* User Data */
         ZBUS_OBSERVERS(gopro_cmd_subscriber),  	    /* observers */
         ZBUS_MSG_INIT(0)       						/* Initial value */
);

ZBUS_CHAN_DEFINE(gopro_query_chan,                        /* Name */
         uint8_t,                       		/* Message type */
         NULL,                           /* Validator */
         NULL,                                       	/* User Data */
         ZBUS_OBSERVERS(gopro_query_subscriber),  	    /* observers */
         ZBUS_MSG_INIT(0)       						/* Initial value */
);


ZBUS_MSG_SUBSCRIBER_DEFINE(gopro_cmd_subscriber);
K_THREAD_DEFINE(gopro_cmd_subscriber_task_id, 1024, gopro_cmd_subscriber_task, NULL, NULL, NULL, 3, 0, 0);

ZBUS_SUBSCRIBER_DEFINE(gopro_query_subscriber,4);
K_THREAD_DEFINE(gopro_query_subscriber_task_id, 1024, gopro_query_subscriber_task, NULL, NULL, NULL, 7, 0, 0);

ZBUS_CHAN_DECLARE(can_tx_chan);

static void gopro_cmd_subscriber_task(void *ptr1, void *ptr2, void *ptr3){
	int err;
	struct gopro_cmd_t gopro_cmd;
	ARG_UNUSED(ptr1);
	ARG_UNUSED(ptr2);
	ARG_UNUSED(ptr3);
	const struct zbus_channel *chan;

	while (!zbus_sub_wait_msg(&gopro_cmd_subscriber, &chan, &gopro_cmd, K_FOREVER)) {
		if (&gopro_cmd_chan == chan) {
				LOG_HEXDUMP_DBG(gopro_cmd.data, gopro_cmd.len,"CMD Data to send:");

				err = bt_gopro_client_send(&gopro_client, &gopro_cmd);

				if (err) {
					LOG_WRN("Failed to send data over BLE connection (err %d)", err);
				}

			}
	}
};


static void gopro_query_subscriber_task(void *ptr1, void *ptr2, void *ptr3){
	int err;
	ARG_UNUSED(ptr1);
	ARG_UNUSED(ptr2);
	ARG_UNUSED(ptr3);
	const struct zbus_channel *chan;
	struct gopro_cmd_t *gopro_cmd;
	int q_msg;

	while (!zbus_sub_wait(&gopro_query_subscriber, &chan, K_FOREVER)) {
		if (&gopro_query_chan == chan) {
				
				int q_msg = k_msgq_num_used_get(&gopro_query_q);

				err = k_msgq_get(&gopro_query_q, &gopro_cmd, K_MSEC(50));

				if((err == 0) && (q_msg > 0)){
					err = zbus_chan_pub(&gopro_cmd_chan, gopro_cmd, K_MSEC(50));
					if(err != 0){
						LOG_ERR("Can't pub query command: %d",err);
						}
				}else{
						LOG_ERR("Failed read Q: %d",err);
					}
		}
	}
};


bool gopro_cmd_validator(const void* msg, size_t msg_size) {
	struct gopro_cmd_t *gopro_cmd =  (struct gopro_cmd_t *)msg;

	if(gopro_client_get_state() != GPSTATE_CONNECTED){
		return 0;
	}

	if(gopro_cmd->len >= GOPRO_CMD_DATA_LEN){
		return 0;
	}

	switch (gopro_cmd->cmd_type)
	{
	case GP_HANDLE_CMD:
	case GP_HANDLE_SETTINGS:
	case GP_HANDLE_QUERY:
		break;
	
	default:
		return 0;
		break;
	}

	return 1;
}

static void ble_data_sent(struct bt_gopro_client *nus, uint8_t err, const uint8_t *const data, uint16_t len)
{
	ARG_UNUSED(nus);
	ARG_UNUSED(data);
	ARG_UNUSED(len);

	LOG_DBG("Data send len: %d",len);

	if (err) {
		LOG_WRN("ATT error code: 0x%02X", err);
	}
}

static uint8_t ble_data_received(struct bt_gopro_client *nus, const struct gopro_cmd_t *gopro_cmd)
{
	struct can_frame tx_frame;
	int data_len;
	ARG_UNUSED(nus);
	int err;

	LOG_INF("Get reply on %d, len %d",gopro_cmd->cmd_type,gopro_cmd->len);
	LOG_HEXDUMP_DBG(gopro_cmd->data,gopro_cmd->len,"Recieve data:");

	if(gopro_cmd->len > 0){
		memset(&tx_frame,0,sizeof(struct can_frame));
		tx_frame.id = GPCAN_REPLY_MSG_ID;

		if(gopro_cmd->data[0] == gopro_cmd->len-1){
			data_len = gopro_cmd->data[0];
			LOG_DBG("Set dlc data[0] %d",data_len);
		}else{
			data_len = gopro_cmd->len;
			LOG_DBG("Frame error, set dlc len %d",data_len);
		}
		
		if(data_len > 8){
			tx_frame.dlc = 8;

			for(uint32_t i=0; i<tx_frame.dlc; i++){
				tx_frame.data[i] = 0xEE;
			}	

			LOG_WRN("Reply to long: %d bytes",data_len);
		}else{
			tx_frame.dlc = data_len;
			
			for(uint32_t i=0; i<tx_frame.dlc; i++){
				tx_frame.data[i] = gopro_cmd->data[i+1];
			}
			
		}

		zbus_chan_pub(&can_tx_chan, &tx_frame, K_NO_WAIT);


	}else{
		LOG_ERR("Can't send reply, len: %d",gopro_cmd->len);
	}

	int q_msg = k_msgq_num_used_get(&gopro_query_q);
	if(q_msg != 0){
		err = zbus_chan_notify(&gopro_query_chan, K_MSEC(10));

		if(err != 0){
			LOG_ERR("Failed notyfy chan: %d, MSG Q: %d",err,q_msg);
		}
	}

	return BT_GATT_ITER_CONTINUE;
}


static void discovery_complete(struct bt_gatt_dm *dm, void *context)
{
	int err;
	struct bt_gopro_client *nus = context;

	LOG_INF("Service discovery completed");

	bt_security_t sec_level_str = bt_conn_get_security(default_conn);
	LOG_DBG("Security Level now: %d",sec_level_str);

	//bt_gatt_dm_data_print(dm);

	bt_gopro_handles_assign(dm, nus);
	bt_gopro_subscribe_receive(nus);
	bt_gatt_dm_data_release(dm);

	LOG_DBG("Set connected mode");

	gopro_led_mode_set(LED_NUM_BT,LED_MODE_ON);
	gopro_client_set_sate(GPSTATE_CONNECTED);
	
	k_sleep(K_MSEC(1000));

	err = zbus_chan_notify(&gopro_query_chan, K_MSEC(10));

	if(err != 0){
		LOG_ERR("Chan notify failed: %d",err);
	}

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
	static struct bt_gatt_exchange_params exchange_params;
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
	
			if(err == BT_SECURITY_ERR_PIN_OR_KEY_MISSING){
				LOG_INF("Remove bonding, start new pair");
				bt_unpair(BT_ID_DEFAULT,BT_ADDR_LE_ANY);
			}
		
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
			
			#ifndef BT_AUTO_CONNECT
			err = bt_scan_stop();
			if (err) {
				LOG_ERR("Failed to stop scanning (err %d)", err);
				return err;
			}

			err = bt_conn_le_create(gopro_client_get_device_addr(), conn_params,BT_LE_CONN_PARAM_DEFAULT,&default_conn);

			if(err != 0){
				LOG_ERR("Conn failed, err: %d",err);
			}
			#endif
			
			break;

		case 5:
			gopro_led_mode_set(LED_NUM_BT,LED_MODE_BLINK_100MS);
			gopro_client_set_sate(GPSTATE_PAIRING);
			LOG_DBG("Camera Pairing");

			#ifndef BT_AUTO_CONNECT
			err = bt_scan_stop();
			if (err) {
				LOG_ERR("Failed to stop scanning (err %d)", err);
				return err;
			}

			err = bt_conn_le_create(gopro_client_get_device_addr(), conn_params,BT_LE_CONN_PARAM_DEFAULT,&default_conn);

			if(err != 0){
				LOG_ERR("Conn failed, err: %d",err);
			}
			#endif

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

	gopro_client_set_device_addr((bt_addr_le_t *)device_info->recv_info->addr);

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
			.unsubscribed = NULL
		}
	};


	for(uint32_t i=0; i < sizeof(startup_query_list)/sizeof(startup_query_list[0]); i++){
		
		LOG_HEXDUMP_DBG(startup_query_list[i]->data,startup_query_list[i]->len,"Push to Q:");
		LOG_DBG("Push pointer 0x%0X",(uint32_t)startup_query_list[i]);
		err = k_msgq_put(&gopro_query_q, &startup_query_list[i], K_MSEC(10));
		if(err != 0){
			LOG_ERR("Failed put to Q: %d",err);
		}
	}

	LOG_DBG("Start-up Q init, messages: %d", k_msgq_num_used_get(&gopro_query_q));

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
		#ifdef BT_AUTO_CONNECT
		.connect_if_match = true,
		#else
		.connect_if_match = false,
		#endif
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
	settings_save();
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

	err = gopro_client_init();
	if (err != 0) {
		LOG_ERR("gopro_client_init failed (err %d)", err);
		return 0;
	}

	settings_subsys_init();
	err=settings_load();
	if(err != 0){
		LOG_ERR("Settings load err: %d",err);
	}else{
		LOG_DBG("Settings load done");
	}

	scan_init();
	err = scan_start();
	if (err) {
		return 0;
	}

	LOG_INF("Starting Bluetooth Central");

	return 0;
}
