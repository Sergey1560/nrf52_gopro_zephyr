#ifndef BT_GOPRO_CLIENT_H_
#define BT_GOPRO_CLIENT_H_

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>

#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>

#include <gopro_ids.h>

#define BLE_UUID16_GOPRO_SERVICE   			0xFEA6      

#define BT_UUID_GOPRO_CMD_WRITE_VAL 		BT_UUID_128_ENCODE(0xb5f90072, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_CMD_NOTIFY_VAL 		BT_UUID_128_ENCODE(0xb5f90073, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)

#define BT_UUID_GOPRO_SETTINGS_WRITE_VAL 	BT_UUID_128_ENCODE(0xb5f90074, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_SETTINGS_NOTIFY_VAL 	BT_UUID_128_ENCODE(0xb5f90075, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)

#define BT_UUID_GOPRO_QUERY_WRITE_VAL 		BT_UUID_128_ENCODE(0xb5f90076, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_QUERY_NOTIFY_VAL 		BT_UUID_128_ENCODE(0xb5f90077, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)

#define BT_UUID_GOPRO_NET_WRITE_VAL 		BT_UUID_128_ENCODE(0xb5f90091, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_NET_NOTIFY_VAL 		BT_UUID_128_ENCODE(0xb5f90092, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)

#define BT_UUID_GOPRO_WIFI_SERVICE_VAL		BT_UUID_128_ENCODE(0xb5f90001, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_NET_SERVICE_VAL		BT_UUID_128_ENCODE(0xb5f90090, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_WIFI_SSID_VAL			BT_UUID_128_ENCODE(0xb5f90002, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_WIFI_PASS_VAL			BT_UUID_128_ENCODE(0xb5f90003, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_WIFI_POWER_VAL		BT_UUID_128_ENCODE(0xb5f90004, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_WIFI_STATE_VAL		BT_UUID_128_ENCODE(0xb5f90005, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)

#define BT_UUID_GOPRO_SERVICE	   			BT_UUID_DECLARE_16(BLE_UUID16_GOPRO_SERVICE)
#define BT_UUID_GOPRO_WIFI_SERVICE	   		BT_UUID_DECLARE_128(BT_UUID_GOPRO_WIFI_SERVICE_VAL)
#define BT_UUID_GOPRO_NET_SERVICE	   		BT_UUID_DECLARE_128(BT_UUID_GOPRO_NET_SERVICE_VAL)

#define BT_UUID_GOPRO_CMD_WRITE        		BT_UUID_DECLARE_128(BT_UUID_GOPRO_CMD_WRITE_VAL)
#define BT_UUID_GOPRO_CMD_NOTIFY       		BT_UUID_DECLARE_128(BT_UUID_GOPRO_CMD_NOTIFY_VAL)

#define BT_UUID_GOPRO_SETTINGS_WRITE     	BT_UUID_DECLARE_128(BT_UUID_GOPRO_SETTINGS_WRITE_VAL)
#define BT_UUID_GOPRO_SETTINGS_NOTIFY      	BT_UUID_DECLARE_128(BT_UUID_GOPRO_SETTINGS_NOTIFY_VAL)

#define BT_UUID_GOPRO_QUERY_WRITE        	BT_UUID_DECLARE_128(BT_UUID_GOPRO_QUERY_WRITE_VAL)
#define BT_UUID_GOPRO_QUERY_NOTIFY       	BT_UUID_DECLARE_128(BT_UUID_GOPRO_QUERY_NOTIFY_VAL)

#define BT_UUID_GOPRO_NET_WRITE        		BT_UUID_DECLARE_128(BT_UUID_GOPRO_NET_WRITE_VAL)
#define BT_UUID_GOPRO_NET_NOTIFY       		BT_UUID_DECLARE_128(BT_UUID_GOPRO_NET_NOTIFY_VAL)

#define BT_UUID_GOPRO_WIFI_SSID				BT_UUID_DECLARE_128(BT_UUID_GOPRO_WIFI_SSID_VAL)
#define BT_UUID_GOPRO_WIFI_PASS				BT_UUID_DECLARE_128(BT_UUID_GOPRO_WIFI_PASS_VAL)
#define BT_UUID_GOPRO_WIFI_POWER			BT_UUID_DECLARE_128(BT_UUID_GOPRO_WIFI_POWER_VAL)
#define BT_UUID_GOPRO_WIFI_STATE			BT_UUID_DECLARE_128(BT_UUID_GOPRO_WIFI_STATE_VAL)

#define GOPRO_NAME_LEN						20
#define GOPRO_CMD_DATA_LEN					20

enum gopro_state_list_t{
    GP_STATE_UNKNOWN,
    GP_STATE_OFFLINE,
    GP_STATE_ONLINE,
    GP_STATE_CONNECTED,
    GP_STATE_NEED_PAIRING,
    GP_STATE_PAIRING,
    GP_STATE_END
};

enum gopro_control_handle_list_t{
    GP_CNTRL_HANDLE_CMD,
    GP_CNTRL_HANDLE_SETTINGS,
    GP_CNTRL_HANDLE_QUERY,
    GP_CNTRL_HANDLE_NET,
	GP_CNTRL_HANDLE_END,
};

enum gopro_wifi_handle_list_t{
    GP_WIFI_HANDLE_SSID,
    GP_WIFI_HANDLE_PASS,
    GP_WIFI_HANDLE_POWER,
    GP_WIFI_HANDLE_STATE,
    GP_WIFI_HANDLE_END,
};

enum gopro_flag_t{
	GP_FLAG_JUST_PAIRED,
	GP_FLAG_CMD_NOTIF_ENABLED,
	GP_FLAG_SETTINGS_NOTIF_ENABLED,
	GP_FLAG_QUERY_NOTIF_ENABLED,
	GP_FLAG_NET_NOTIF_ENABLED,
	GP_FLAG_CMD_WRITE_PENDING,
	GP_FLAG_SETTINGS_WRITE_PENDING,
	GP_FLAG_QUERY_WRITE_PENDING,
	GP_FLAG_NET_WRITE_PENDING
};

struct gopro_state_t {
	struct bt_scan_device_info 	device_info;
	bt_addr_le_t 				addr;
	enum gopro_state_list_t  	state;
	uint32_t					video_count;
	uint8_t						record;
	uint8_t						battery;
	char 						name[GOPRO_NAME_LEN];
	char 						wifi_ssid[20];
	char 						wifi_pass[20];
    char 						model_name[20];
    char 						firmware_version[20];
    char 						serial_number[20];
    char 						ap_mac[20];
};

struct gopro_cmd_t {
	uint32_t len;
	uint32_t cmd_type;
	uint8_t  data[GOPRO_CMD_DATA_LEN];
};

struct mem_pkt_t{
    uint32_t index;
    int len;
    uint8_t *data;
};

struct bt_gopro_client_handles {
	uint16_t write;			
	uint16_t notify;		
	uint16_t notify_ccc; 	
};

struct bt_gopro_client {
	struct bt_conn *conn;
	atomic_t state;
	uint32_t just_paired;
	struct bt_gopro_client_handles handles[GP_CNTRL_HANDLE_END];
	uint16_t wifihandles[GP_WIFI_HANDLE_END];
	struct bt_gatt_subscribe_params notif_params[GP_CNTRL_HANDLE_END];
	struct bt_gatt_write_params 	write_params[GP_CNTRL_HANDLE_END];
	struct bt_gatt_read_params 		read_wifi_params[GP_WIFI_HANDLE_END];
};

void gopro_client_update_state(void);

int gopro_client_set_device_addr(bt_addr_le_t* addr);
bt_addr_le_t* gopro_client_get_device_addr(void);

int gopro_client_set_sate(enum gopro_state_list_t  state);
enum gopro_state_list_t gopro_client_get_state(void);

int gopro_client_setname(char *name, uint8_t len);

int bt_gopro_net_handle_assign(struct bt_gatt_dm *dm,  struct bt_gopro_client *nus_c);
int bt_gopro_handles_assign(struct bt_gatt_dm *dm, struct bt_gopro_client *nus);
int bt_gopro_wifi_handles_assign(struct bt_gatt_dm *dm,  struct bt_gopro_client *nus_c);

int gopro_set_subscribe(struct bt_gopro_client *nus_c, enum gopro_control_handle_list_t gopro_handle);

int bt_gopro_client_send(struct bt_gopro_client *nus, struct gopro_cmd_t *gopro_cmd);
int bt_gopro_client_get(struct bt_gopro_client *nus_c, uint16_t handle);


#endif
