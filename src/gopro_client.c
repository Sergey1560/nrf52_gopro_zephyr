/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/zbus/zbus.h>

#include <gopro_client.h>
#include <leds.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gopro_c, LOG_LEVEL_DBG);

ZBUS_CHAN_DEFINE(gopro_state_chan,                     	/* Name */
	struct gopro_state_t,                   		      	/* Message type */
	NULL,                                       	/* Validator */
	NULL,                                       	/* User Data */
	ZBUS_OBSERVERS_EMPTY,  	        		/* observers */
	ZBUS_MSG_INIT(0)       						/* Initial value */
);

static int gopro_parse_query_status_notify(const void *data, uint16_t length);
static int gopro_check_reply(struct gopro_cmd_t *gopro_cmd);

static uint8_t on_received_cmd(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length);
static uint8_t on_received_settings(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length);
static uint8_t on_received_query(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length);
static uint8_t on_received_net(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length);

uint8_t (*notify_func[GP_CNTRL_HANDLE_END])(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length) = {on_received_cmd, on_received_settings, on_received_query,on_received_net};

static void on_sent_cmd(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params);
static void on_sent_settings(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params);
static void on_sent_query(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params);
static void on_sent_net(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params);

static uint8_t on_read_default(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length);
static uint8_t on_read_ssid(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length);
static uint8_t on_read_pass(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length);

void (*sent_func[GP_CNTRL_HANDLE_END])(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params) = {on_sent_cmd,on_sent_settings,on_sent_query,on_sent_net};
uint8_t (*read_func[GP_WIFI_HANDLE_END])(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length) = {on_read_ssid,on_read_pass,on_read_default,on_read_default};

static void gopro_client_update_state(void);

static struct gopro_state_t gopro_state;

K_SEM_DEFINE(ble_read_sem, 0, 1);

int gopro_client_set_device_addr(bt_addr_le_t* addr){

	gopro_state.addr = *addr;
	return 0;
}

bt_addr_le_t* gopro_client_get_device_addr(void){

	bt_addr_le_t *addr = &gopro_state.addr;
	
	return addr;
}

int gopro_client_set_sate(enum gopro_state_list_t  state){

	if(state >= GPSTATE_END){
		LOG_ERR("Invalid state num %d of %d",state,GPSTATE_END-1);
		return -1;
	}

	gopro_state.state = state;

	LOG_DBG("Set GoPro state: %d", gopro_state.state);

	gopro_client_update_state();

	return 0;
}


enum gopro_state_list_t gopro_client_get_state(void){

	return gopro_state.state;
}


int gopro_client_setname(char *name, uint8_t len){

	if(len >= GOPRO_NAME_LEN){
		LOG_ERR("GoPro name out of bound %d of %d",len,GOPRO_NAME_LEN-1);
		return -1;
	}

	if( (len > 0) && (name != NULL) ){
		if(strncmp(gopro_state.name,name,len) == 0){
			LOG_DBG("GoPro name already set");
			return 0;
		}

		memcpy(gopro_state.name,name,len);
		gopro_state.name[len]=0;

		LOG_DBG("Set GoPro name: %s", gopro_state.name);
	}else{
		memset(gopro_state.name,0,GOPRO_NAME_LEN);
	}
	
	gopro_client_update_state();

	return 0;
}


static void gopro_client_update_state(void){
	zbus_chan_pub(&gopro_state_chan, &gopro_state, K_MSEC(10));
};


static uint8_t on_received_cmd(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length)
{
	struct bt_gopro_client *nus;
	struct gopro_cmd_t gopro_cmd;
	uint8_t *pdata = (uint8_t *)data;

	nus = CONTAINER_OF(params, struct bt_gopro_client, notif_params[GP_CNTRL_HANDLE_CMD]);

	if (!data) {
		LOG_DBG("[UNSUBSCRIBED]");
		params->value_handle = 0;
		
		atomic_clear_bit(&nus->state, GOPRO_C_CMD_NOTIF_ENABLED);
		
		if (nus->cb.unsubscribed) {
			nus->cb.unsubscribed(nus);
		}
		
		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("[NOTIFICATION] length %u handle %d", length, params->value_handle);
	
	if (nus->cb.received) {
		gopro_cmd.cmd_type = GP_CNTRL_HANDLE_CMD;
		
		if(length > GOPRO_CMD_DATA_LEN){
			gopro_cmd.len = GOPRO_CMD_DATA_LEN;
			LOG_WRN("Reply Len > Buf Len");
		}else{
			gopro_cmd.len = length;
		}

		for(uint32_t i=0; i<gopro_cmd.len; i++){
			gopro_cmd.data[i]=(uint8_t)(pdata[i]);
		}

		return nus->cb.received(nus, &gopro_cmd);
	}

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t on_received_settings(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length)
{
	struct bt_gopro_client *nus;
	struct gopro_cmd_t gopro_cmd;
	uint8_t *pdata = (uint8_t *)data;

	nus = CONTAINER_OF(params, struct bt_gopro_client, notif_params[GP_CNTRL_HANDLE_SETTINGS]);

	if (!data) {
		LOG_DBG("[UNSUBSCRIBED]");
		params->value_handle = 0;

		atomic_clear_bit(&nus->state, GOPRO_C_SETTINGS_NOTIF_ENABLED);
		if (nus->cb.unsubscribed) {
			nus->cb.unsubscribed(nus);
		}
		
		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("[NOTIFICATION] length %u handle %d", length, params->value_handle);
	
	if (nus->cb.received) {
			gopro_cmd.cmd_type = GP_CNTRL_HANDLE_SETTINGS;
			
			if(length > GOPRO_CMD_DATA_LEN){
				gopro_cmd.len = GOPRO_CMD_DATA_LEN;
				LOG_WRN("Reply Len > Buf Len");
			}else{
				gopro_cmd.len = length;
			}

			for(uint32_t i=0; i<gopro_cmd.len; i++){
				gopro_cmd.data[i]=(uint8_t)(pdata[i]);
			}

		return nus->cb.received(nus, &gopro_cmd);
	}

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t on_received_query(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length)
{
	struct bt_gopro_client *nus;
	struct gopro_cmd_t gopro_cmd;
	uint8_t *pdata = (uint8_t *)data;

	nus = CONTAINER_OF(params, struct bt_gopro_client, notif_params[GP_CNTRL_HANDLE_QUERY]);

	if (!data) {
		LOG_DBG("[UNSUBSCRIBED]");
		params->value_handle = 0;
		
		atomic_clear_bit(&nus->state, GOPRO_C_QUERY_NOTIF_ENABLED);
		
		if (nus->cb.unsubscribed) {
			nus->cb.unsubscribed(nus);
		}
		
		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("[NOTIFICATION] length %u handle %d", length, params->value_handle);
	if (nus->cb.received) {
			gopro_cmd.cmd_type = GP_CNTRL_HANDLE_QUERY;
		
			if(length > GOPRO_CMD_DATA_LEN){
				gopro_cmd.len = GOPRO_CMD_DATA_LEN;
				LOG_WRN("Reply Len > Buf Len");
			}else{
				gopro_cmd.len = length;
			}

			for(uint32_t i=0; i<gopro_cmd.len; i++){
				gopro_cmd.data[i]=(uint8_t)(pdata[i]);
			}

			gopro_parse_query_status_notify(data,length);

		return nus->cb.received(nus, &gopro_cmd);
	}

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t on_received_net(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length)
{
	struct bt_gopro_client *nus;
	struct gopro_cmd_t gopro_cmd;
	uint8_t *pdata = (uint8_t *)data;

	nus = CONTAINER_OF(params, struct bt_gopro_client, notif_params[GP_CNTRL_HANDLE_NET]);

	if (!data) {
		LOG_DBG("[UNSUBSCRIBED]");
		params->value_handle = 0;
		
		atomic_clear_bit(&nus->state, GOPRO_C_NET_NOTIF_ENABLED);
		
		if (nus->cb.unsubscribed) {
			nus->cb.unsubscribed(nus);
		}
		
		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("[NOTIFICATION] length %u handle %d", length, params->value_handle);
	
	if (nus->cb.received) {
		gopro_cmd.cmd_type = GP_CNTRL_HANDLE_NET;
		
		if(length > GOPRO_CMD_DATA_LEN){
			gopro_cmd.len = GOPRO_CMD_DATA_LEN;
			LOG_WRN("Reply Len > Buf Len");
		}else{
			gopro_cmd.len = length;
		}

		for(uint32_t i=0; i<gopro_cmd.len; i++){
			gopro_cmd.data[i]=(uint8_t)(pdata[i]);
		}

		return nus->cb.received(nus, &gopro_cmd);
	}

	return BT_GATT_ITER_CONTINUE;
}

static void on_sent_cmd(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params){
	struct bt_gopro_client *nus_c;
	const void *data;
	uint16_t length;

	/* Retrieve module context. */
	nus_c = CONTAINER_OF(params, struct bt_gopro_client, write_params[GP_CNTRL_HANDLE_CMD]);

	/* Make a copy of volatile data that is required by the callback. */
	data = params->data;
	length = params->length;

	atomic_clear_bit(&nus_c->state, GOPRO_C_CMD_WRITE_PENDING);

	if (nus_c->cb.sent) {
		nus_c->cb.sent(nus_c, err, data, length);
	}
}

static void on_sent_settings(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params){
	struct bt_gopro_client *nus_c;
	const void *data;
	uint16_t length;

	/* Retrieve module context. */
	nus_c = CONTAINER_OF(params, struct bt_gopro_client, write_params[GP_CNTRL_HANDLE_SETTINGS]);

	/* Make a copy of volatile data that is required by the callback. */
	data = params->data;
	length = params->length;

	atomic_clear_bit(&nus_c->state, GOPRO_C_SETTINGS_WRITE_PENDING);

	if (nus_c->cb.sent) {
		nus_c->cb.sent(nus_c, err, data, length);
	}
}

static void on_sent_query(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params){
	struct bt_gopro_client *nus_c;
	const void *data;
	uint16_t length;

	/* Retrieve module context. */
	nus_c = CONTAINER_OF(params, struct bt_gopro_client, write_params[GP_CNTRL_HANDLE_QUERY]);

	/* Make a copy of volatile data that is required by the callback. */
	data = params->data;
	length = params->length;

	atomic_clear_bit(&nus_c->state, GOPRO_C_QUERY_WRITE_PENDING);

	if (nus_c->cb.sent) {
		nus_c->cb.sent(nus_c, err, data, length);
	}
}

static void on_sent_net(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params){
	struct bt_gopro_client *nus_c;
	const void *data;
	uint16_t length;

	/* Retrieve module context. */
	nus_c = CONTAINER_OF(params, struct bt_gopro_client, write_params[GP_CNTRL_HANDLE_NET]);

	/* Make a copy of volatile data that is required by the callback. */
	data = params->data;
	length = params->length;

	atomic_clear_bit(&nus_c->state, GOPRO_C_NET_WRITE_PENDING);

	if (nus_c->cb.sent) {
		nus_c->cb.sent(nus_c, err, data, length);
	}
}


int bt_gopro_client_init(struct bt_gopro_client *nus_c, const struct bt_gopro_client_init_param *nus_c_init)
{
	if (!nus_c || !nus_c_init) {
		return -EINVAL;
	}

	if (atomic_test_and_set_bit(&nus_c->state, GOPRO_C_INITIALIZED)) {
		return -EALREADY;
	}

	memcpy(&nus_c->cb, &nus_c_init->cb, sizeof(nus_c->cb));

	return 0;
}

int bt_gopro_client_send(struct bt_gopro_client *nus_c, struct gopro_cmd_t *gopro_cmd){
	int err;
	int flag_bit;
	int handle_index;

	if (!nus_c->conn) {
		return -ENOTCONN;
	}

	if(gopro_cmd->cmd_type >= GP_CNTRL_HANDLE_END){
		return -ENOTSUP;
	}

	handle_index = gopro_cmd->cmd_type;

	switch (handle_index)
	{
	case GP_CNTRL_HANDLE_CMD:
		flag_bit = GOPRO_C_CMD_WRITE_PENDING;
		break;
	case GP_CNTRL_HANDLE_SETTINGS:
		flag_bit = GOPRO_C_SETTINGS_WRITE_PENDING;
		break;
	case GP_CNTRL_HANDLE_QUERY:
		flag_bit = GOPRO_C_QUERY_WRITE_PENDING;
		break;
	case GP_CNTRL_HANDLE_NET:
		flag_bit = GOPRO_C_NET_WRITE_PENDING;
		break;
	
	default:
		LOG_ERR("Invalid cmd type: %d",gopro_cmd->cmd_type);
		return -ENOTSUP;
		break;
	}

	if (atomic_test_and_set_bit(&nus_c->state, flag_bit)) {
		return -EALREADY;
	}

	LOG_DBG("Send handle: 0x%0X %d bytes",nus_c->write_params[handle_index].handle, gopro_cmd->len);

	nus_c->write_params[handle_index].handle = nus_c->handles[handle_index].write;
	nus_c->write_params[handle_index].func = sent_func[handle_index];
	nus_c->write_params[handle_index].offset = 0;
	nus_c->write_params[handle_index].data = gopro_cmd->data;
	nus_c->write_params[handle_index].length = gopro_cmd->len;

	err = bt_gatt_write(nus_c->conn, &nus_c->write_params[handle_index]);
	if (err) {
		atomic_clear_bit(&nus_c->state, flag_bit);
	}

	return err;
}

static uint8_t on_read_ssid(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length){
	struct bt_gopro_client *nus_c;

	/* Retrieve module context. */
	nus_c = CONTAINER_OF(params, struct bt_gopro_client, read_wifi_params[GP_WIFI_HANDLE_SSID]);
	
	k_sem_give(&ble_read_sem);

	if (err) {
		LOG_ERR("Read char error %d",err);
		return BT_GATT_ITER_STOP;
	}

	//LOG_DBG("Handle: %d SSID handle: %d",params->single.handle, nus_c->wifihandles[GP_WIFI_HANDLE_SSID]);

	if(params->single.handle == nus_c->wifihandles[GP_WIFI_HANDLE_SSID]){
		memset(gopro_state.wifi_ssid,0,sizeof(gopro_state.wifi_ssid));
		
		uint8_t str_size = (length >= (sizeof(gopro_state.wifi_ssid)+1)) ? sizeof(gopro_state.wifi_ssid)-1 : length;
		memcpy(gopro_state.wifi_ssid,data,str_size);
		gopro_state.wifi_ssid[str_size]=0;
		
		LOG_DBG("Get AP SSID %s",gopro_state.wifi_ssid);
	}else{
		LOG_HEXDUMP_DBG(data,length,"Read CHAR data:");
	}

	return BT_GATT_ITER_STOP;
}

static uint8_t on_read_pass(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length){
	struct bt_gopro_client *nus_c;

	/* Retrieve module context. */
	nus_c = CONTAINER_OF(params, struct bt_gopro_client, read_wifi_params[GP_WIFI_HANDLE_PASS]);

	k_sem_give(&ble_read_sem);
	
	if (err) {
		LOG_ERR("Read char error %d",err);
		return BT_GATT_ITER_STOP;
	}

	//LOG_DBG("Handle: %d PASS handle: %d",params->single.handle, nus_c->wifihandles[GP_WIFI_HANDLE_PASS]);

	if(params->single.handle == nus_c->wifihandles[GP_WIFI_HANDLE_PASS]){
		memset(gopro_state.wifi_pass,0,sizeof(gopro_state.wifi_pass));
		
		uint8_t str_size = (length >= (sizeof(gopro_state.wifi_pass)+1)) ? sizeof(gopro_state.wifi_pass)-1 : length;
		memcpy(gopro_state.wifi_pass,data,str_size);
		gopro_state.wifi_pass[str_size]=0;
		
		LOG_DBG("Get AP PASS %s",gopro_state.wifi_pass);
	}else{
		LOG_HEXDUMP_DBG(data,length,"Read CHAR data:");
	}

	return BT_GATT_ITER_STOP;
}

static uint8_t on_read_default(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length){
	
	if (err) {
		LOG_ERR("Read char error %d",err);
		return BT_GATT_ITER_STOP;
	}

	LOG_HEXDUMP_DBG(data,length,"Read CHAR data:");

	return BT_GATT_ITER_STOP;
}


int bt_gopro_client_get(struct bt_gopro_client *nus_c, uint16_t handle){
	int err;

	if (!nus_c->conn) {
		return -ENOTCONN;
	}

	if(handle >= GP_WIFI_HANDLE_END){
		return -1;
	}

	nus_c->read_wifi_params[handle].func=read_func[handle];
	nus_c->read_wifi_params[handle].single.handle=nus_c->wifihandles[handle];
	nus_c->read_wifi_params[handle].single.offset=0;
	nus_c->read_wifi_params[handle].handle_count=1;

	err = bt_gatt_read(nus_c->conn,&nus_c->read_wifi_params[handle]);
	if(err){
		LOG_ERR("Failed read gatt, err (%d) %s", err, bt_gatt_err_to_str(err));
	}

	return err;
}

/*
static int gopro_set_handle(struct bt_gatt_dm *dm, struct bt_gopro_client *nus_c, enum gopro_handle_list_t gopro_handle){
	const struct bt_gatt_dm_attr *gatt_chrc;
	const struct bt_gatt_dm_attr *gatt_desc;
	static struct bt_uuid *notify_uuid = NULL;
	static struct bt_uuid *write_uuid = NULL;

	switch (gopro_handle)
	{
	case GP_HANDLE_CMD:
		notify_uuid = BT_UUID_GOPRO_CMD_NOTIFY;
		write_uuid  = BT_UUID_GOPRO_CMD_WRITE;
		break;

	case GP_HANDLE_SETTINGS:
		notify_uuid = BT_UUID_GOPRO_SETTINGS_NOTIFY;
		write_uuid  = BT_UUID_GOPRO_SETTINGS_WRITE;
		break;
	
	case GP_HANDLE_QUERY:
		notify_uuid = BT_UUID_GOPRO_QUERY_NOTIFY;
		write_uuid  = BT_UUID_GOPRO_QUERY_WRITE;
		break;
		
	default:
		return -EINVAL;	
		break;
	}

	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, notify_uuid);
	LOG_DBG("DM: 0x%0X UUID: 0x%0X",(uint32_t)dm,(uint32_t)notify_uuid);
	LOG_DBG("Char by uuid = %d",gatt_chrc);
	if (!gatt_chrc) {
		LOG_ERR("Missing characteristic for %d",gopro_handle);
		return -EINVAL;
	}

	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, notify_uuid);
	if (!gatt_desc) {
		LOG_ERR("Missing Notify value descriptor in characteristic for %d.",gopro_handle);
		return -EINVAL;
	}
	
	LOG_DBG("Found handle for Notify characteristic for %d.",gopro_handle);
	nus_c->handles[gopro_handle].notify = gatt_desc->handle;

	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Notify CCC in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for CCC of GoPro Notify characteristic. 0x%0X",gatt_desc->handle);
	nus_c->handles[gopro_handle].notify_ccc = gatt_desc->handle;

	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, write_uuid);
	if (!gatt_chrc) {
		LOG_ERR("Missing Write characteristic.");
		return -EINVAL;
	}

	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, write_uuid);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Write value descriptor in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for Write characteristic.");
	nus_c->handles[gopro_handle].write = gatt_desc->handle;

	LOG_INF("Handles assigned succesfull.");
	return 0;
}
*/
int bt_gopro_wifi_handles_assign(struct bt_gatt_dm *dm,  struct bt_gopro_client *nus_c){
	const struct bt_gatt_dm_attr *gatt_service_attr = bt_gatt_dm_service_get(dm);
	const struct bt_gatt_service_val *gatt_service = bt_gatt_dm_attr_service_val(gatt_service_attr);
	struct bt_gatt_dm_attr *gatt_chrc;
	struct bt_gatt_dm_attr *gatt_desc;


	if (bt_uuid_cmp(gatt_service->uuid, BT_UUID_GOPRO_WIFI_SERVICE)) {
		LOG_ERR("Not valid GoPro WIFI Service UUID");
		return -ENOTSUP;
	}
	memset(&nus_c->wifihandles, 0xFF, sizeof(nus_c->wifihandles));


	/* SSID */
	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_WIFI_SSID);
	LOG_DBG("DM: 0x%0X UUID: 0x%0X",(uint32_t)dm,(uint32_t)BT_UUID_GOPRO_WIFI_SSID);
	if (!gatt_chrc) {
		LOG_ERR("Missing characteristic for SSID");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_WIFI_SSID);
	if (!gatt_desc) {
		LOG_ERR("Missing descriptor in characteristic for SSID.");
		return -EINVAL;
	}
	
	LOG_DBG("Found handle for SSID characteristic");
	nus_c->wifihandles[GP_WIFI_HANDLE_SSID] = gatt_desc->handle;


	/* PASS */
	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_WIFI_PASS);
	LOG_DBG("DM: 0x%0X UUID: 0x%0X",(uint32_t)dm,(uint32_t)BT_UUID_GOPRO_WIFI_PASS);
	if (!gatt_chrc) {
		LOG_ERR("Missing characteristic for PASS");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_WIFI_PASS);
	if (!gatt_desc) {
		LOG_ERR("Missing descriptor in characteristic for PASS.");
		return -EINVAL;
	}
	
	LOG_DBG("Found handle for PASS characteristic");
	nus_c->wifihandles[GP_WIFI_HANDLE_PASS] = gatt_desc->handle;


	/* POWER */
	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_WIFI_POWER);
	LOG_DBG("DM: 0x%0X UUID: 0x%0X",(uint32_t)dm,(uint32_t)BT_UUID_GOPRO_WIFI_POWER);
	if (!gatt_chrc) {
		LOG_ERR("Missing characteristic for POWER");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_WIFI_POWER);
	if (!gatt_desc) {
		LOG_ERR("Missing descriptor in characteristic for POWER.");
		return -EINVAL;
	}
	
	LOG_DBG("Found handle for POWER characteristic");
	nus_c->wifihandles[GP_WIFI_HANDLE_POWER] = gatt_desc->handle;

	/* STATE */
	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_WIFI_STATE);
	LOG_DBG("DM: 0x%0X UUID: 0x%0X",(uint32_t)dm,(uint32_t)BT_UUID_GOPRO_WIFI_STATE);
	if (!gatt_chrc) {
		LOG_ERR("Missing characteristic for STATE");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_WIFI_STATE);
	if (!gatt_desc) {
		LOG_ERR("Missing descriptor in characteristic for STATE.");
		return -EINVAL;
	}
	
	LOG_DBG("Found handle for STATE characteristic");
	nus_c->wifihandles[GP_WIFI_HANDLE_STATE] = gatt_desc->handle;



	nus_c->conn = bt_gatt_dm_conn_get(dm);
	return 0;
};

int bt_gopro_handles_assign(struct bt_gatt_dm *dm,  struct bt_gopro_client *nus_c){
	const struct bt_gatt_dm_attr *gatt_service_attr = bt_gatt_dm_service_get(dm);
	const struct bt_gatt_service_val *gatt_service = bt_gatt_dm_attr_service_val(gatt_service_attr);
	struct bt_gatt_dm_attr *gatt_chrc;
	struct bt_gatt_dm_attr *gatt_desc;

	if (bt_uuid_cmp(gatt_service->uuid, BT_UUID_GOPRO_SERVICE)) {
		LOG_ERR("Not valid GoPro Service UUID");
		return -ENOTSUP;
	}
	memset(&nus_c->handles, 0xFF, sizeof(nus_c->handles));

	/* CMD */
	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_CMD_NOTIFY);
	LOG_DBG("DM: 0x%0X UUID: 0x%0X",(uint32_t)dm,(uint32_t)BT_UUID_GOPRO_CMD_NOTIFY);
	if (!gatt_chrc) {
		LOG_ERR("Missing Notify characteristic for CMD");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_CMD_NOTIFY);
	if (!gatt_desc) {
		LOG_ERR("Missing Notify value descriptor in characteristic for CMD.");
		return -EINVAL;
	}
	
	LOG_DBG("Found handle for Notify characteristic for CMD.");
	nus_c->handles[GP_CNTRL_HANDLE_CMD].notify = gatt_desc->handle;

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Notify CCC in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for CCC of GoPro Notify characteristic. 0x%0X",gatt_desc->handle);
	nus_c->handles[GP_CNTRL_HANDLE_CMD].notify_ccc = gatt_desc->handle;

	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_CMD_WRITE);
	if (!gatt_chrc) {
		LOG_ERR("Missing Write characteristic.");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_CMD_WRITE);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Write value descriptor in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for Write characteristic.");
	nus_c->handles[GP_CNTRL_HANDLE_CMD].write = gatt_desc->handle;

	LOG_INF("CMD Handles assigned succesfull.");
	/* CMD */
	//gopro_set_handle(dm, nus_c, GP_HANDLE_CMD);
	
	
	/* SETTINGS */
	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_SETTINGS_NOTIFY);
	LOG_DBG("DM: 0x%0X UUID: 0x%0X",(uint32_t)dm,(uint32_t)BT_UUID_GOPRO_SETTINGS_NOTIFY);
	if (!gatt_chrc) {
		LOG_ERR("Missing Notify characteristic for SETTINGS");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_SETTINGS_NOTIFY);
	if (!gatt_desc) {
		LOG_ERR("Missing Notify value descriptor in characteristic for SETTINGS.");
		return -EINVAL;
	}
	
	LOG_DBG("Found handle for Notify characteristic for SETTINGS.");
	nus_c->handles[GP_CNTRL_HANDLE_SETTINGS].notify = gatt_desc->handle;

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Notify CCC in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for CCC of GoPro Notify characteristic. 0x%0X",gatt_desc->handle);
	nus_c->handles[GP_CNTRL_HANDLE_SETTINGS].notify_ccc = gatt_desc->handle;

	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_SETTINGS_WRITE);
	if (!gatt_chrc) {
		LOG_ERR("Missing Write characteristic.");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_SETTINGS_WRITE);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Write value descriptor in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for Write characteristic.");
	nus_c->handles[GP_CNTRL_HANDLE_SETTINGS].write = gatt_desc->handle;

	LOG_INF("SETTINGS Handles assigned succesfull.");
	/* SETTINGS */
	//gopro_set_handle(dm, nus_c, GP_HANDLE_SETTINGS);



	/* QUERY */
	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_QUERY_NOTIFY);
	LOG_DBG("DM: 0x%0X UUID: 0x%0X",(uint32_t)dm,(uint32_t)BT_UUID_GOPRO_QUERY_NOTIFY);
	if (!gatt_chrc) {
		LOG_ERR("Missing Notify characteristic for QUERY");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_QUERY_NOTIFY);
	if (!gatt_desc) {
		LOG_ERR("Missing Notify value descriptor in characteristic for QUERY.");
		return -EINVAL;
	}
	
	LOG_DBG("Found handle for Notify characteristic for QUERY.");
	nus_c->handles[GP_CNTRL_HANDLE_QUERY].notify = gatt_desc->handle;

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Notify CCC in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for CCC of GoPro Notify characteristic. 0x%0X",gatt_desc->handle);
	nus_c->handles[GP_CNTRL_HANDLE_QUERY].notify_ccc = gatt_desc->handle;

	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_QUERY_WRITE);
	if (!gatt_chrc) {
		LOG_ERR("Missing Write characteristic.");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_QUERY_WRITE);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Write value descriptor in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for Write characteristic.");
	nus_c->handles[GP_CNTRL_HANDLE_QUERY].write = gatt_desc->handle;

	LOG_INF("QUERY Handles assigned succesfull.");
	/* CMD */
	//gopro_set_handle(dm, nus_c, GP_HANDLE_QUERY);

	/* Assign connection instance. */
	nus_c->conn = bt_gatt_dm_conn_get(dm);
	return 0;
}

int bt_gopro_net_handle_assign(struct bt_gatt_dm *dm,  struct bt_gopro_client *nus_c){
	const struct bt_gatt_dm_attr *gatt_service_attr = bt_gatt_dm_service_get(dm);
	const struct bt_gatt_service_val *gatt_service = bt_gatt_dm_attr_service_val(gatt_service_attr);
	struct bt_gatt_dm_attr *gatt_chrc;
	struct bt_gatt_dm_attr *gatt_desc;

	if (bt_uuid_cmp(gatt_service->uuid, BT_UUID_GOPRO_NET_SERVICE)) {
		LOG_ERR("Not valid GoPro Net UUID");
		return -ENOTSUP;
	}
	memset(&nus_c->handles[GP_CNTRL_HANDLE_NET], 0xFF, sizeof(nus_c->handles[GP_CNTRL_HANDLE_NET]));

	/* Network Management Response */
	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_NET_NOTIFY);
	LOG_DBG("DM: 0x%0X UUID: 0x%0X",(uint32_t)dm,(uint32_t)BT_UUID_GOPRO_NET_NOTIFY);
	if (!gatt_chrc) {
		LOG_ERR("Missing Notify characteristic for NET");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_NET_NOTIFY);
	if (!gatt_desc) {
		LOG_ERR("Missing Notify value descriptor in characteristic for NET.");
		return -EINVAL;
	}
	
	LOG_DBG("Found handle for Notify characteristic for NET.");
	nus_c->handles[GP_CNTRL_HANDLE_NET].notify = gatt_desc->handle;

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Notify CCC in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for CCC of GoPro Notify characteristic. 0x%0X",gatt_desc->handle);
	nus_c->handles[GP_CNTRL_HANDLE_NET].notify_ccc = gatt_desc->handle;

	gatt_chrc = (struct bt_gatt_dm_attr *)bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_NET_WRITE);
	if (!gatt_chrc) {
		LOG_ERR("Missing Write characteristic.");
		return -EINVAL;
	}

	gatt_desc = (struct bt_gatt_dm_attr *)bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_NET_WRITE);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Write value descriptor in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for Write characteristic.");
	nus_c->handles[GP_CNTRL_HANDLE_NET].write = gatt_desc->handle;

	LOG_INF("NET Handles assigned succesfull.");

	/* Assign connection instance. */
	nus_c->conn = bt_gatt_dm_conn_get(dm);
	return 0;
}


static int gopro_set_subscribe(struct bt_gopro_client *nus_c, enum gopro_control_handle_list_t gopro_handle){
	int flag_bit;
	int handle_index;
	int err;

	switch (gopro_handle)
	{
	case GP_CNTRL_HANDLE_CMD:
		flag_bit = GOPRO_C_CMD_NOTIF_ENABLED;
		handle_index = GP_CNTRL_HANDLE_CMD;
		break;
	
	case GP_CNTRL_HANDLE_SETTINGS:
		flag_bit = GOPRO_C_SETTINGS_NOTIF_ENABLED;
		handle_index = GP_CNTRL_HANDLE_SETTINGS;
		break;
	
	case GP_CNTRL_HANDLE_QUERY:
		flag_bit = GOPRO_C_QUERY_NOTIF_ENABLED;
		handle_index = GP_CNTRL_HANDLE_QUERY;
		break;

	case GP_CNTRL_HANDLE_NET:
		flag_bit = GOPRO_C_NET_NOTIF_ENABLED;
		handle_index = GP_CNTRL_HANDLE_NET;
		break;
		
	default:
		return -1;
		break;
	}

	if (atomic_test_and_set_bit(&nus_c->state, flag_bit)) {
		LOG_ERR("Subs error");
		return -EALREADY;
	}

	nus_c->notif_params[handle_index].notify = notify_func[handle_index];
	nus_c->notif_params[handle_index].value = BT_GATT_CCC_NOTIFY;
	nus_c->notif_params[handle_index].value_handle = nus_c->handles[handle_index].notify;
	nus_c->notif_params[handle_index].ccc_handle = nus_c->handles[handle_index].notify_ccc;
	
	atomic_set_bit(nus_c->notif_params[handle_index].flags,BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	err = bt_gatt_subscribe(nus_c->conn, &nus_c->notif_params[handle_index]);

	if (err) {
		LOG_ERR("Subscribe failed (err %d)", err);
		atomic_clear_bit(&nus_c->state, flag_bit);
	} else {
		LOG_DBG("[SUBSCRIBED] for %d handle",handle_index);
	}

	return 0;
}

// static int gopro_net_subscribe(struct bt_gopro_client *nus_c){
// 	int flag_bit;
// 	int err;

// 	flag_bit = GOPRO_C_NET_NOTIF_ENABLED;

// 	if (atomic_test_and_set_bit(&nus_c->state, flag_bit)) {
// 		LOG_ERR("Subs error");
// 		return -EALREADY;
// 	}

// 	nus_c->net_notif_params.notify = on_received_net;
// 	nus_c->net_notif_params.value = BT_GATT_CCC_NOTIFY;
// 	nus_c->net_notif_params.value_handle = nus_c->net_handle.notify;
// 	nus_c->net_notif_params.ccc_handle = nus_c->net_handle.notify_ccc;
	
// 	atomic_set_bit(nus_c->net_notif_params.flags,BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

// 	err = bt_gatt_subscribe(nus_c->conn, &nus_c->net_notif_params);

// 	if (err) {
// 		LOG_ERR("Subscribe failed (err %d)", err);
// 		atomic_clear_bit(&nus_c->state, flag_bit);
// 	} else {
// 		LOG_DBG("[SUBSCRIBED] for NET handle");
// 	}

// 	return 0;
// }

int bt_gopro_subscribe_receive(struct bt_gopro_client *nus_c)
{
	int err;

	err=gopro_set_subscribe(nus_c, GP_CNTRL_HANDLE_CMD);
	err=gopro_set_subscribe(nus_c, GP_CNTRL_HANDLE_SETTINGS);
	err=gopro_set_subscribe(nus_c, GP_CNTRL_HANDLE_QUERY);
	err=gopro_set_subscribe(nus_c, GP_CNTRL_HANDLE_NET);

	//err=gopro_net_subscribe(nus_c);

	return err;
}


static int gopro_parse_query_status_notify(const void *data, uint16_t length){
	uint8_t *pdata = (uint8_t *)data;
	int total_data_len;
	int result;
	int id;
	int id_len;

	if(pdata[0] != (length-1)){
		LOG_ERR("Not REPLY FMT");
		return -1;
	}

	total_data_len = pdata[0];
	pdata++;

	id = *pdata++;
	result = *pdata++;

	if( (id != GOPRO_QUERY_STATUS_REG_STATUS_NOTIFY) && (id != GOPRO_QUERY_STATUS_REG_STATUS) ){
		LOG_WRN("Not REG packet. CMD: 0x%0X",(int)id);
		return -1;
	}

	if((result != 0)){
		LOG_WRN("Not valid result. CMD: 0x%0X result: %d",id,result);
		return -1;
	}

	total_data_len = total_data_len - 3;

	while(total_data_len > 0){

		id = *pdata++;
		id_len = *pdata++;

		total_data_len = total_data_len - 2;

		switch (id)
		{
		case GOPRO_STATUS_ID_ENCODING:
			gopro_state.record = *pdata++;
			total_data_len--;
			LOG_DBG("Encoding: %d",gopro_state.record);

			if(gopro_state.record > 0){
				gopro_led_mode_set(LED_NUM_REC,LED_MODE_ON);
			}else{
				gopro_led_mode_set(LED_NUM_REC,LED_MODE_OFF);
			}

			break;

		case GOPRO_STATUS_ID_VIDEO_NUM:
			gopro_state.video_count = 0;
			for(uint32_t i=0; i<id_len; i++){
				total_data_len--;
				gopro_state.video_count = (gopro_state.video_count*256) + *pdata++;
			}	
			LOG_DBG("Video Count: %d",gopro_state.video_count);
			break;

		case GOPRO_STATUS_ID_BAT_PERCENT:
			gopro_state.battery = *pdata++;
			total_data_len--;
			LOG_DBG("Battery: %d",gopro_state.battery);
			break;
	

		default:
			LOG_WRN("Unknown status %d (0x%0X)",id,id);
			pdata += id_len;
			total_data_len -= id_len;
			break;
		}
	}

	gopro_client_update_state();

	return 0;
}


static int gopro_parse_query_status_reply(const void *data, uint16_t length){
	uint8_t *pdata = (uint8_t *)data;
	int total_data_len;
	int result;
	int id;

	if(pdata[0] != (length-1)){
		LOG_ERR("Not REPLY FMT");
		return -1;
	}

	total_data_len = pdata[0];
	id = pdata[1];
	result = pdata[2];

	uint8_t status_id = pdata[3];
	uint8_t status_len = pdata[4];

	switch (status_id)
	{
	case GOPRO_STATUS_ID_VIDEO_NUM:
		gopro_state.video_count = 0;
		for(uint32_t i=0; i<status_len; i++){
			gopro_state.video_count = (gopro_state.video_count*256) + pdata[5+i];
		}	
		LOG_DBG("Video count: %d",gopro_state.video_count);
		break;

	case GOPRO_STATUS_ID_BAT_PERCENT:
		gopro_state.battery = pdata[5];
		LOG_DBG("Battery: %d",gopro_state.battery);
		break;

	case GOPRO_STATUS_ID_ENCODING:
		gopro_state.record = pdata[5];
		LOG_DBG("Encoding: %d",gopro_state.record);

		if(gopro_state.record > 0){
			gopro_led_mode_set(LED_NUM_REC,LED_MODE_ON);
		}else{
			gopro_led_mode_set(LED_NUM_REC,LED_MODE_OFF);
		}

		break;

	default:
		LOG_WRN("Unknown status %d (0x%0X)",id,id);
		break;
	}

	gopro_client_update_state();

	return 0;
}

static int gopro_check_reply(struct gopro_cmd_t *gopro_cmd){

	if(gopro_cmd->len == (gopro_cmd->data[0]+1) ){
		return 0;
	}


	return -1;
}


int	gopro_parse_query_reply(struct gopro_cmd_t *gopro_cmd){

	LOG_DBG("Parse Query reply");

	if(gopro_check_reply(gopro_cmd) != 0){
		LOG_ERR("Not valid reply");
		return -1;
	}

	switch (gopro_cmd->data[1])
	{
	case GOPRO_QUERY_STATUS_REG_STATUS:
	case GOPRO_QUERY_STATUS_REG_STATUS_NOTIFY:
		if(gopro_cmd->data[2] == 0){
			gopro_parse_query_status_notify(gopro_cmd->data,gopro_cmd->len);
		}else{
			LOG_ERR("REG Result not OK: %d",gopro_cmd->data[2]);
		}	
		return 0;
		break;

	case GOPRO_QUERY_STATUS_GET_STATUS:
		if(gopro_cmd->data[2] == 0){
			gopro_parse_query_status_reply(gopro_cmd->data,gopro_cmd->len);
		}else{
			LOG_ERR("REG Result not OK: %d",gopro_cmd->data[2]);
		}	

		break;

	default:
		break;
	}


	return 0;
};

int gopro_parse_settings_reply(struct gopro_cmd_t *gopro_cmd){
	LOG_DBG("SETTINGS reply");

	return 0;
};

int gopro_parse_cmd_reply(struct gopro_cmd_t *gopro_cmd){

	LOG_DBG("CMD reply");
	return 0;
};
