/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <gopro_client.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gopro_c, LOG_LEVEL_DBG);

enum {
	GOPRO_C_INITIALIZED,
	GOPRO_C_TX_NOTIF_ENABLED,
	GOPRO_C_RX_WRITE_PENDING
};

static uint8_t on_received(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data, uint16_t length)
{
	struct bt_gopro_client *nus;

	/* Retrieve NUS Client module context. */
	nus = CONTAINER_OF(params, struct bt_gopro_client, cmd_notif_params);

	if (!data) {
		LOG_DBG("[UNSUBSCRIBED]");
		params->value_handle = 0;
		atomic_clear_bit(&nus->state, GOPRO_C_TX_NOTIF_ENABLED);
		if (nus->cb.unsubscribed) {
			nus->cb.unsubscribed(nus);
		}
		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("[NOTIFICATION] data %p length %u", data, length);
	if (nus->cb.received) {
		return nus->cb.received(nus, data, length);
	}

	return BT_GATT_ITER_CONTINUE;
}

static void on_sent(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	struct bt_gopro_client *nus_c;
	const void *data;
	uint16_t length;

	/* Retrieve NUS Client module context. */
	nus_c = CONTAINER_OF(params, struct bt_gopro_client, cmd_write_params);

	/* Make a copy of volatile data that is required by the callback. */
	data = params->data;
	length = params->length;

	atomic_clear_bit(&nus_c->state, GOPRO_C_RX_WRITE_PENDING);
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

int bt_gopro_client_send(struct bt_gopro_client *nus_c, const uint8_t *data, uint16_t len)
{
	int err;

	if (!nus_c->conn) {
		return -ENOTCONN;
	}

	if (atomic_test_and_set_bit(&nus_c->state, GOPRO_C_RX_WRITE_PENDING)) {
		return -EALREADY;
	}

	LOG_INF("Send handle: 0x%0X %d bytes",nus_c->handles.gp072,len);

	nus_c->cmd_write_params.func = on_sent;
	nus_c->cmd_write_params.handle = nus_c->handles.gp072;
	nus_c->cmd_write_params.offset = 0;
	nus_c->cmd_write_params.data = data;
	nus_c->cmd_write_params.length = len;

	err = bt_gatt_write(nus_c->conn, &nus_c->cmd_write_params);
	if (err) {
		atomic_clear_bit(&nus_c->state, GOPRO_C_RX_WRITE_PENDING);
	}

	return err;
}

int bt_gopro_handles_assign(struct bt_gatt_dm *dm,  struct bt_gopro_client *nus_c){
	const struct bt_gatt_dm_attr *gatt_service_attr = bt_gatt_dm_service_get(dm);
	const struct bt_gatt_service_val *gatt_service = bt_gatt_dm_attr_service_val(gatt_service_attr);
	const struct bt_gatt_dm_attr *gatt_chrc;
	const struct bt_gatt_dm_attr *gatt_desc;

	if (bt_uuid_cmp(gatt_service->uuid, BT_UUID_GOPRO_SERVICE)) {
		LOG_ERR("Not valid GoPro Service UUID");
		return -ENOTSUP;
	}
	LOG_DBG("Getting handles from GOPRO service.");
	memset(&nus_c->handles, 0xFF, sizeof(nus_c->handles));

	/* CMD Reply Characteristic */
	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_CMD_NOTIFY);
	if (!gatt_chrc) {
		LOG_ERR("Missing GP0073 characteristic.");
		return -EINVAL;
	}
	/* CMD Reply */
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_CMD_NOTIFY);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Notify value descriptor in characteristic.");
		return -EINVAL;
	}
	
	LOG_INF("Found handle for GoPro Notify characteristic.");
	nus_c->handles.gp073 = gatt_desc->handle;
	/* NUS TX CCC */
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Notify CCC in characteristic.");
		return -EINVAL;
	}
	LOG_INF("Found handle for CCC of GoPro Notify characteristic. 0x%0X",gatt_desc->handle);
	nus_c->handles.gp073_ccc = gatt_desc->handle;

	/* CMD Write Characteristic */
	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_CMD_WRITE);
	if (!gatt_chrc) {
		LOG_ERR("Missing GoPro Write characteristic.");
		return -EINVAL;
	}
	/* CMD Write */
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_CMD_WRITE);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Write value descriptor in characteristic.");
		return -EINVAL;
	}
	LOG_INF("Found handle for GoPro Write characteristic.");
	nus_c->handles.gp072 = gatt_desc->handle;



	/* Settings Characteristic */
	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_SETTINGS_NOTIFY);
	if (!gatt_chrc) {
		LOG_ERR("Missing GP0075 characteristic.");
		return -EINVAL;
	}
	/* Reply */
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_SETTINGS_NOTIFY);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Settings Notify value descriptor in characteristic.");
		return -EINVAL;
	}
	
	LOG_INF("Found handle for GoPro Settings  Notify characteristic.");
	nus_c->handles.gp075 = gatt_desc->handle;
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);

	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Settings Notify CCC in characteristic.");
		return -EINVAL;
	}
	LOG_INF("Found handle for CCC of GoPro Settings Notify characteristic. 0x%0X",gatt_desc->handle);
	nus_c->handles.gp075_ccc = gatt_desc->handle;



	/* Query Characteristic */
	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_GOPRO_QUERY_NOTIFY);
	if (!gatt_chrc) {
		LOG_ERR("Missing GP0077 characteristic.");
		return -EINVAL;
	}
	/* Reply */
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GOPRO_QUERY_NOTIFY);
	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Query Notify value descriptor in characteristic.");
		return -EINVAL;
	}
	
	LOG_INF("Found handle for GoPro Query  Notify characteristic.");
	nus_c->handles.gp077 = gatt_desc->handle;
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);

	if (!gatt_desc) {
		LOG_ERR("Missing GoPro Query Notify CCC in characteristic.");
		return -EINVAL;
	}
	LOG_INF("Found handle for CCC of GoPro Query Notify characteristic. 0x%0X",gatt_desc->handle);
	nus_c->handles.gp077_ccc = gatt_desc->handle;



	/* Assign connection instance. */
	nus_c->conn = bt_gatt_dm_conn_get(dm);
	return 0;
}

int bt_gopro_subscribe_receive(struct bt_gopro_client *nus_c)
{
	int err;

	if (atomic_test_and_set_bit(&nus_c->state, GOPRO_C_TX_NOTIF_ENABLED)) {
		LOG_ERR("Subs error");
		return -EALREADY;
	}

	nus_c->cmd_notif_params.notify = on_received;
	nus_c->cmd_notif_params.value = BT_GATT_CCC_NOTIFY;
	nus_c->cmd_notif_params.value_handle = nus_c->handles.gp073;
	nus_c->cmd_notif_params.ccc_handle = nus_c->handles.gp073_ccc;
	atomic_set_bit(nus_c->cmd_notif_params.flags,BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	err = bt_gatt_subscribe(nus_c->conn, &nus_c->cmd_notif_params);

	if (err) {
		LOG_ERR("Subscribe GP0073 failed (err %d)", err);
		atomic_clear_bit(&nus_c->state, GOPRO_C_TX_NOTIF_ENABLED);
	} else {
		LOG_DBG("GP073 [SUBSCRIBED]");
	}


	nus_c->settings_notif_params.notify = on_received;
	nus_c->settings_notif_params.value = BT_GATT_CCC_NOTIFY;
	nus_c->settings_notif_params.value_handle = nus_c->handles.gp075;
	nus_c->settings_notif_params.ccc_handle = nus_c->handles.gp075_ccc;
	atomic_set_bit(nus_c->settings_notif_params.flags,BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	err = bt_gatt_subscribe(nus_c->conn, &nus_c->settings_notif_params);

	if (err) {
		LOG_ERR("Subscribe GP0075 failed (err %d)", err);
		atomic_clear_bit(&nus_c->state, GOPRO_C_TX_NOTIF_ENABLED);
	} else {
		LOG_DBG("GP0075 [SUBSCRIBED]");
	}


	nus_c->query_notif_params.notify = on_received;
	nus_c->query_notif_params.value = BT_GATT_CCC_NOTIFY;
	nus_c->query_notif_params.value_handle = nus_c->handles.gp077;
	nus_c->query_notif_params.ccc_handle = nus_c->handles.gp077_ccc;
	atomic_set_bit(nus_c->query_notif_params.flags,BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	err = bt_gatt_subscribe(nus_c->conn, &nus_c->query_notif_params);

	if (err) {
		LOG_ERR("Subscribe GP0077 failed (err %d)", err);
		atomic_clear_bit(&nus_c->state, GOPRO_C_TX_NOTIF_ENABLED);
	} else {
		LOG_DBG("GP0077 [SUBSCRIBED]");
	}


	return err;
}
