#ifndef BT_GOPRO_CLIENT_H_
#define BT_GOPRO_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <bluetooth/gatt_dm.h>

#define BLE_UUID16_GOPRO_SERVICE    0xFEA6      

#define BT_UUID_GOPRO_CMD_WRITE_VAL 		BT_UUID_128_ENCODE(0xb5f90072, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_CMD_NOTIFY_VAL 		BT_UUID_128_ENCODE(0xb5f90073, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)

#define BT_UUID_GOPRO_SETTINGS_WRITE_VAL 	BT_UUID_128_ENCODE(0xb5f90074, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_SETTINGS_NOTIFY_VAL 	BT_UUID_128_ENCODE(0xb5f90075, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)

#define BT_UUID_GOPRO_QUERY_WRITE_VAL 		BT_UUID_128_ENCODE(0xb5f90076, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_QUERY_NOTIFY_VAL 		BT_UUID_128_ENCODE(0xb5f90077, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)


#define BT_UUID_GOPRO_SERVICE	   			BT_UUID_DECLARE_16(BLE_UUID16_GOPRO_SERVICE)

#define BT_UUID_GOPRO_CMD_WRITE        		BT_UUID_DECLARE_128(BT_UUID_GOPRO_CMD_WRITE_VAL)
#define BT_UUID_GOPRO_CMD_NOTIFY       		BT_UUID_DECLARE_128(BT_UUID_GOPRO_CMD_NOTIFY_VAL)

#define BT_UUID_GOPRO_SETTINGS_WRITE     	BT_UUID_DECLARE_128(BT_UUID_GOPRO_SETTINGS_WRITE_VAL)
#define BT_UUID_GOPRO_SETTINGS_NOTIFY      	BT_UUID_DECLARE_128(BT_UUID_GOPRO_SETTINGS_NOTIFY_VAL)

#define BT_UUID_GOPRO_QUERY_WRITE        	BT_UUID_DECLARE_128(BT_UUID_GOPRO_QUERY_WRITE_VAL)
#define BT_UUID_GOPRO_QUERY_NOTIFY       	BT_UUID_DECLARE_128(BT_UUID_GOPRO_QUERY_NOTIFY_VAL)

#define GOPRO_NAME_LEN	12

enum gopro_state_list_t{
    GPSTATE_UNKNOWN,
    GPSTATE_OFFLINE,
    GPSTATE_ONLINE,
    GPSTATE_CONNECTED,
    GPSTATE_PAIRING,
    GPSTATE_END
};

struct gopro_state_t {
	struct bt_scan_device_info *device_info;
	enum gopro_state_list_t  state;
	char name[GOPRO_NAME_LEN];
};


/** @brief Handles on the connected peer device that are needed to interact with
 * the device.
 */
struct bt_gopro_client_handles {
	uint16_t gp072;			/* Handle of the GP-0072 Command characteristic */
	uint16_t gp073;			/* Handle of the GP-0073 characteristic */
	uint16_t gp073_ccc; 	/* Handle of the CCC descriptor of the GP0073 characteristic */

	uint16_t gp074;			/* Handle of the GP-0074 Settings characteristic */
	uint16_t gp075;			/* Handle of the GP-0075 characteristic */
	uint16_t gp075_ccc; 	/* Handle of the CCC descriptor of the GP0075 characteristic */

	uint16_t gp076;			/* Handle of the GP-0074 Settings characteristic */
	uint16_t gp077;			/* Handle of the GP-0075 characteristic */
	uint16_t gp077_ccc; 	/* Handle of the CCC descriptor of the GP0075 characteristic */
};

struct bt_gopro_client;

/** @brief NUS Client callback structure. */
struct bt_gopro_client_cb {
	/** @brief Data received callback.
	 *
	 * The data has been received as a notification of the NUS TX
	 * Characteristic.
	 *
	 * @param[in] nus  NUS Client instance.
	 * @param[in] data Received data.
	 * @param[in] len Length of received data.
	 *
	 * @retval BT_GATT_ITER_CONTINUE To keep notifications enabled.
	 * @retval BT_GATT_ITER_STOP To disable notifications.
	 */
	uint8_t (*received)(struct bt_gopro_client *nus, const uint8_t *data, uint16_t len);

	/** @brief Data sent callback.
	 *
	 * The data has been sent and written to the NUS RX Characteristic.
	 *
	 * @param[in] nus  NUS Client instance.
	 * @param[in] err ATT error code.
	 * @param[in] data Transmitted data.
	 * @param[in] len Length of transmitted data.
	 */
	void (*sent)(struct bt_gopro_client *nus, uint8_t err, const uint8_t *data, uint16_t len);

	/** @brief TX notifications disabled callback.
	 *
	 * TX notifications have been disabled.
	 *
	 * @param[in] nus  NUS Client instance.
	 */
	void (*unsubscribed)(struct bt_gopro_client *nus);
};

/** @brief GoPro Client structure. */
struct bt_gopro_client {

        /** Connection object. */
	struct bt_conn *conn;

        /** Internal state. */
	atomic_t state;

        /** Handles on the connected peer device that are needed
         * to interact with the device.
         */
	struct bt_gopro_client_handles handles;

        /** GATT subscribe parameters for NUS TX Characteristic. */
	struct bt_gatt_subscribe_params cmd_notif_params;
	struct bt_gatt_subscribe_params settings_notif_params;
	struct bt_gatt_subscribe_params query_notif_params;

        /** GATT write parameters for NUS RX Characteristic. */
	struct bt_gatt_write_params cmd_write_params;
	struct bt_gatt_write_params settings_write_params;
	struct bt_gatt_write_params query_write_params;

        /** Application callbacks. */
	struct bt_gopro_client_cb cb;
};

/** @brief NUS Client initialization structure. */
struct bt_gopro_client_init_param {

        /** Callbacks provided by the user. */
	struct bt_gopro_client_cb cb;
};


int gopro_client_set_device_info(struct bt_scan_device_info *device_info);
struct bt_scan_device_info* gopro_client_get_device_info(void);
int gopro_client_set_sate(enum gopro_state_list_t  state);
int gopro_client_setname(char *name, uint8_t len);

/** @brief Initialize the GoPro Client module.
 *
 * This function initializes the GoPro Client module with callbacks provided by
 * the user.
 *
 * @param[in,out] nus    Client instance.
 * @param[in] init_param Client initialization parameters.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a negative error code is returned.
 */
int bt_gopro_client_init(struct bt_gopro_client *nus, const struct bt_gopro_client_init_param *init_param);

/** @brief Send data to the server.
 *
 * This function writes command to the Characteristic of the GoPro.
 *
 * @note This procedure is asynchronous. Therefore, the data to be sent must
 * remain valid while the function is active.
 *
 * @param[in,out] nus Client instance.
 * @param[in] data Data to be transmitted.
 * @param[in] len Length of data.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a negative error code is returned.
 */
int bt_gopro_client_send(struct bt_gopro_client *nus, const uint8_t *data, uint16_t len);

/** @brief Assign handles to the GoPro Client instance.
 *
 * This function should be called when a link with a peer has been established
 * to associate the link to this instance of the module. This makes it
 * possible to handle several links and associate each link to a particular
 * instance of this module. The GATT attribute handles are provided by the
 * GATT DB discovery module.
 *
 * @param[in] dm Discovery object.
 * @param[in,out] nus Client instance.
 *
 * @retval 0 If the operation was successful.
 * @retval (-ENOTSUP) Special error code used when UUID
 *         of the service does not match the expected UUID.
 * @retval Otherwise, a negative error code is returned.
 */
int bt_gopro_handles_assign(struct bt_gatt_dm *dm, struct bt_gopro_client *nus);

/** @brief Request the peer to start sending notifications for the Notify
 *	   Characteristic.
 *
 * This function enables notifications for the Characteristic at the peer
 * by writing to the CCC descriptor of the TX Characteristic.
 *
 * @param[in,out] nus Client instance.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a negative error code is returned.
 */
int bt_gopro_subscribe_receive(struct bt_gopro_client *nus);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* BT_GOPRO_CLIENT_H_ */
