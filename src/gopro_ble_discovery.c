#include "gopro_ble_discovery.h"

#include <leds.h>

#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gopro_discovery, LOG_LEVEL_DBG);

const struct bt_uuid *uuid_list[] = {BT_UUID_GOPRO_WIFI_SERVICE,BT_UUID_GOPRO_SERVICE};
#define UUID_COUNT  (sizeof(uuid_list)/sizeof(uuid_list[0]))

ZBUS_CHAN_DECLARE(gopro_cmd_chan);

K_SEM_DEFINE(discovery_sem, 0, 1);

static void discovery_complete(struct bt_gatt_dm *dm, void *context);
static void discovery_service_not_found(struct bt_conn *conn, void *context);
static void discovery_error(struct bt_conn *conn, int err,void *context);

static int handles_assign(struct bt_gatt_dm *dm,  struct bt_gopro_client *nus_c);

static void discovery_work_handler(struct k_work *work);

const struct bt_gatt_dm_cb discovery_cb = {
	.completed         = discovery_complete,
	.service_not_found = discovery_service_not_found,
	.error_found       = discovery_error
};

const static struct gopro_cmd_t gopro_query_encoding = {
	.len = 3,
	.cmd_type = GP_CNTRL_HANDLE_QUERY,
	.data={2,GOPRO_QUERY_STATUS_GET_STATUS,GOPRO_STATUS_ID_ENCODING}
};

const static struct gopro_cmd_t gopro_query_battery = {
	.len = 3,
	.cmd_type = GP_CNTRL_HANDLE_QUERY,
	.data={2,GOPRO_QUERY_STATUS_GET_STATUS,GOPRO_STATUS_ID_BAT_PERCENT}
};

const static struct gopro_cmd_t gopro_query_video_num = {
	.len = 3,
	.cmd_type = GP_CNTRL_HANDLE_QUERY,
	.data={2,GOPRO_QUERY_STATUS_GET_STATUS,GOPRO_STATUS_ID_VIDEO_NUM}
};

const static struct gopro_cmd_t gopro_query_register = {
	.len = 5,
	.cmd_type = GP_CNTRL_HANDLE_QUERY,
	.data={4,GOPRO_QUERY_STATUS_REG_STATUS,GOPRO_STATUS_ID_ENCODING,GOPRO_STATUS_ID_VIDEO_NUM,GOPRO_STATUS_ID_BAT_PERCENT}
};

const static struct gopro_cmd_t *startup_query_list[] = {
	&gopro_query_register, 
	&gopro_query_video_num, 
	&gopro_query_battery, 
	&gopro_query_encoding
};

K_WORK_DEFINE(discovery_work, discovery_work_handler);

void gopro_start_discovery(struct bt_conn *conn, struct bt_gopro_client *gopro_client){
    int err;
    char uuid_str[50];

	LOG_INF("Start discovery");

    for(uint32_t i=0; i<UUID_COUNT; i++){
        bt_uuid_to_str(uuid_list[i], uuid_str, 50);
        LOG_DBG("Start discovery UUID [%d] %s",i,uuid_str);

        err = bt_gatt_dm_start(conn, BT_UUID_GOPRO_WIFI_SERVICE, &discovery_cb, gopro_client);
        if (err) {
            LOG_ERR("could not start the discovery procedure, error code: %d", err);
        }

		err = k_sem_take(&discovery_sem, DISCOVERY_TIMEOUT);
		if (err) {
			LOG_ERR("Discovery timeout");
		}

    }

	bt_gopro_subscribe_receive(gopro_client);
	k_work_submit(&discovery_work);
}

static void discovery_complete(struct bt_gatt_dm *dm, void *context){
	struct bt_gopro_client *nus = context;

	LOG_INF("Service discovery complete");

	bt_gatt_dm_data_print(dm);
	handles_assign(dm, nus);
	bt_gatt_dm_data_release(dm);
	k_sem_give(&discovery_sem);
}

static void discovery_work_handler(struct k_work *work){
	int err;

	LOG_DBG("Set connected mode");
	gopro_led_mode_set(LED_NUM_BT,LED_MODE_ON);
	gopro_client_set_sate(GPSTATE_CONNECTED);

	LOG_DBG("Dummy wait");
	k_sleep(K_MSEC(1000));

	for(uint32_t i=0; i < sizeof(startup_query_list)/sizeof(startup_query_list[0]); i++){
		
		LOG_HEXDUMP_DBG(startup_query_list[i]->data,startup_query_list[i]->len,"Push to TX chan:");
		err = zbus_chan_pub(&gopro_cmd_chan, startup_query_list[i], K_MSEC(100));

		if(err != 0){
			LOG_ERR("Chan pub failed: %d",err);
		}
	}
}

static void discovery_service_not_found(struct bt_conn *conn, void *context){
	LOG_WRN("Service not found");
	k_sem_give(&discovery_sem);
}

static void discovery_error(struct bt_conn *conn, int err,void *context){
	LOG_WRN("Error while discovering GATT database: (%d)", err);
	k_sem_give(&discovery_sem);
}

static int handles_assign(struct bt_gatt_dm *dm,  struct bt_gopro_client *nus_c){
    char uuid_str[50];
	const struct bt_gatt_dm_attr *gatt_service_attr = bt_gatt_dm_service_get(dm);
	const struct bt_gatt_service_val *gatt_service = bt_gatt_dm_attr_service_val(gatt_service_attr);
	int ret_val = -1;

	if (bt_uuid_cmp(gatt_service->uuid, BT_UUID_GOPRO_SERVICE)) {
		ret_val=bt_gopro_handles_assign(dm, nus_c);
	}else if(bt_uuid_cmp(gatt_service->uuid, BT_UUID_GOPRO_WIFI_SERVICE)){
		ret_val=bt_gopro_wifi_handles_assign(dm, nus_c);
	}else{
		bt_uuid_to_str(gatt_service->uuid, uuid_str, 50);
		LOG_WRN("No worker for UUID %s",uuid_str);
	}

	return ret_val;
}
