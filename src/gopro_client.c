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
#include <gopro_packet.h>
#include <gopro_protobuf.h>
#include <leds.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gopro_c, CONFIG_BLE_LOG_LVL);

K_SEM_DEFINE(ble_write_sem, 0, 1);

extern struct bt_gopro_client gopro_client;

static uint8_t on_read_default(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length);
static uint8_t on_read_ssid(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length);
static uint8_t on_read_pass(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length);

uint8_t (*read_func[GP_WIFI_HANDLE_END])(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length) = {on_read_ssid,on_read_pass,on_read_default,on_read_default};

struct gopro_state_t gopro_state;

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

	if(state >= GP_STATE_END){
		LOG_ERR("Invalid state num %d of %d", state, GP_STATE_END-1);
		return -1;
	}

	if(gopro_state.state != state){
		gopro_state.state = state;
		LOG_DBG("Set GoPro state: %d", gopro_state.state);
		//gopro_client_update_state();
	}

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
			//LOG_DBG("GoPro name already set");
			return 0;
		}

		memcpy(gopro_state.name,name,len);
		gopro_state.name[len]=0;

		LOG_DBG("Set GoPro name: %s", gopro_state.name);
	}else{
		memset(gopro_state.name,0,GOPRO_NAME_LEN);
	}
	
	//gopro_client_update_state();

	return 0;
}

// void gopro_client_update_state(void){
// 	//zbus_chan_pub(&gopro_state_chan, &gopro_state, K_NO_WAIT);
// };

static uint8_t on_notify_received(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length)
{
	struct gopro_cmd_t gopro_cmd;
	uint8_t *pdata = (uint8_t *)data;

	if (!data) {
		LOG_DBG("[UNSUBSCRIBED]");

		if(params->value_handle == gopro_client.notif_params[GP_CNTRL_HANDLE_SETTINGS].value_handle){
			LOG_DBG("Setting notify");
			params->value_handle = 0;
			atomic_clear_bit(&gopro_client.state, GP_FLAG_SETTINGS_NOTIF_ENABLED);
		}else if (params->value_handle == gopro_client.notif_params[GP_CNTRL_HANDLE_CMD].value_handle){
			LOG_DBG("CMD notify");
			params->value_handle = 0;
			atomic_clear_bit(&gopro_client.state, GP_FLAG_CMD_NOTIF_ENABLED);
		}else if (params->value_handle == gopro_client.notif_params[GP_CNTRL_HANDLE_QUERY].value_handle){
			LOG_DBG("Query notify");
			params->value_handle = 0;
			atomic_clear_bit(&gopro_client.state, GP_FLAG_QUERY_NOTIF_ENABLED);
		}else if (params->value_handle == gopro_client.notif_params[GP_CNTRL_HANDLE_NET].value_handle){
			LOG_DBG("Net notify");
			params->value_handle = 0;
			atomic_clear_bit(&gopro_client.state, GP_FLAG_NET_NOTIF_ENABLED);
		}else{
			LOG_ERR("Recieve unknown handle 0x%0X",params->value_handle);
			return BT_GATT_ITER_CONTINUE;
		}

		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("[NOTIFICATION] length %u handle 0x%0X", length, params->value_handle);

	gopro_cmd.cmd_type = 0xFF;
	
	for(uint32_t i=0; i<GP_CNTRL_HANDLE_END; i++){
		if(params->value_handle == gopro_client.notif_params[i].value_handle){
			gopro_cmd.cmd_type = i;
			break;	
		}
	}

	if(gopro_cmd.cmd_type == 0xFF){
		LOG_ERR("Recieve unknown handle 0x%0X",params->value_handle);
		return BT_GATT_ITER_CONTINUE;
	}

	if(length > GOPRO_CMD_DATA_LEN){
		gopro_cmd.len = GOPRO_CMD_DATA_LEN;
		LOG_WRN("Reply Len > Buf Len");
	}else{
		gopro_cmd.len = length;
	}

	for(uint32_t i=0; i<gopro_cmd.len; i++){
		gopro_cmd.data[i]=(uint8_t)(pdata[i]);
	}

	gopro_packet_build(&gopro_cmd);

	return BT_GATT_ITER_CONTINUE;
}

static void on_sent_data(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params){
	
	if(params->handle == gopro_client.write_params[GP_CNTRL_HANDLE_SETTINGS].handle){
		LOG_DBG("Setiings sent");
		atomic_clear_bit(&gopro_client.state, GP_FLAG_SETTINGS_WRITE_PENDING);
	}else if (params->handle == gopro_client.write_params[GP_CNTRL_HANDLE_CMD].handle){
		LOG_DBG("CMD sent");
		atomic_clear_bit(&gopro_client.state, GP_FLAG_CMD_WRITE_PENDING);
	}else if (params->handle == gopro_client.write_params[GP_CNTRL_HANDLE_QUERY].handle){
		LOG_DBG("Query sent");
		atomic_clear_bit(&gopro_client.state, GP_FLAG_QUERY_WRITE_PENDING);
	}else if (params->handle == gopro_client.write_params[GP_CNTRL_HANDLE_NET].handle){
		LOG_DBG("Net sent");
		atomic_clear_bit(&gopro_client.state, GP_FLAG_NET_WRITE_PENDING);
	}else{
		LOG_ERR("Recieve unknown handle 0x%0X",params->handle);
		return;
	}

	//atomic_clear_bit(&gopro_client.state, GP_FLAG_CMD_WRITE_PENDING);
	k_sem_give(&ble_write_sem);

	if (err) {
		LOG_WRN("ATT error code: 0x%02X", err);
	}

}

int bt_gopro_client_send(struct bt_gopro_client *gp_client, struct gopro_cmd_t *gopro_cmd){
	int err;
	uint32_t flag_bit;
	int handle_index;

	if (!gp_client->conn) {
		LOG_ERR("No connection");
		return -ENOTCONN;
	}

	if(gopro_cmd->cmd_type >= GP_CNTRL_HANDLE_END){
		LOG_ERR("Unknown cmd type");
		return -ENOTSUP;
	}

	handle_index = gopro_cmd->cmd_type;

	switch (handle_index)
	{
	case GP_CNTRL_HANDLE_CMD:
		flag_bit = GP_FLAG_CMD_WRITE_PENDING;
		break;
	case GP_CNTRL_HANDLE_SETTINGS:
		flag_bit = GP_FLAG_SETTINGS_WRITE_PENDING;
		break;
	case GP_CNTRL_HANDLE_QUERY:
		flag_bit = GP_FLAG_QUERY_WRITE_PENDING;
		break;
	case GP_CNTRL_HANDLE_NET:
		flag_bit = GP_FLAG_NET_WRITE_PENDING;
		break;
	
	default:
		LOG_ERR("Invalid cmd type: %d",gopro_cmd->cmd_type);
		return -ENOTSUP;
		break;
	}

	if (atomic_test_and_set_bit(&gp_client->state, flag_bit)) {
		LOG_ERR("Flag already set");
		return -EALREADY;
	}

	LOG_DBG("Send handle: 0x%0X %d bytes",gp_client->write_params[handle_index].handle, gopro_cmd->len);

	gp_client->write_params[handle_index].handle = gp_client->handles[handle_index].write;
	gp_client->write_params[handle_index].func = on_sent_data;
	gp_client->write_params[handle_index].offset = 0;
	gp_client->write_params[handle_index].data = gopro_cmd->data;
	gp_client->write_params[handle_index].length = gopro_cmd->len;

	err = bt_gatt_write(gp_client->conn, &gp_client->write_params[handle_index]);
	if (err) {
		atomic_clear_bit(&gp_client->state, flag_bit);
		LOG_ERR("Gatt write failed: %d",err);
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
		
		LOG_INF("Get AP SSID %s",gopro_state.wifi_ssid);
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
		
		LOG_INF("Get AP PASS %s",gopro_state.wifi_pass);
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

int gopro_set_subscribe(struct bt_gopro_client *gp_client, enum gopro_control_handle_list_t gopro_handle){
	int flag_bit;
	int err;

	switch (gopro_handle)
	{
	case GP_CNTRL_HANDLE_CMD:
		flag_bit = GP_FLAG_CMD_NOTIF_ENABLED;
		break;
	
	case GP_CNTRL_HANDLE_SETTINGS:
		flag_bit = GP_FLAG_SETTINGS_NOTIF_ENABLED;
		break;
	
	case GP_CNTRL_HANDLE_QUERY:
		flag_bit = GP_FLAG_QUERY_NOTIF_ENABLED;
		break;

	case GP_CNTRL_HANDLE_NET:
		flag_bit = GP_FLAG_NET_NOTIF_ENABLED;
		break;
		
	default:
		return -1;
		break;
	}

	if (atomic_test_and_set_bit(&gp_client->state, flag_bit)) {
		LOG_ERR("Subs error");
		return -EALREADY;
	}

	gp_client->notif_params[gopro_handle].notify = on_notify_received;
	gp_client->notif_params[gopro_handle].value = BT_GATT_CCC_NOTIFY;
	gp_client->notif_params[gopro_handle].value_handle = gp_client->handles[gopro_handle].notify;
	gp_client->notif_params[gopro_handle].ccc_handle = gp_client->handles[gopro_handle].notify_ccc;
	
	atomic_set_bit(gp_client->notif_params[gopro_handle].flags,BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	err = bt_gatt_subscribe(gp_client->conn, &gp_client->notif_params[gopro_handle]);

	if (err) {
		LOG_ERR("Subscribe failed (err %d)", err);
		atomic_clear_bit(&gp_client->state, flag_bit);
	} else {
		LOG_DBG("[SUBSCRIBED] for %d handle",gopro_handle);
	}

	return 0;
}
