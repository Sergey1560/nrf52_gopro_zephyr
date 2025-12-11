#include "gopro_control.h"

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>

#include "gopro_protobuf.h"

LOG_MODULE_REGISTER(gopro_control, CONFIG_BLE_LOG_LVL);
extern struct bt_gopro_client gopro_client;
extern struct gopro_state_t gopro_state;

int gopro_ctrl_parse(struct gopro_cmd_t *gopro_cmd){
    int ret_value = 0;

    if((gopro_cmd->cmd_type == 0xFF) && (gopro_cmd->len==1)){

        switch (gopro_cmd->data[0])
        {
        case 0xDA:
            LOG_WRN("Remove bonding, start new pair");
            bt_unpair(BT_ID_DEFAULT,BT_ADDR_LE_ANY);
            ret_value = 1;
            break;

        case 0xAF:
            LOG_INF("Force connect CMD");
            atomic_set_bit(&gopro_client.state,GP_FLAG_FORCE_CONNECT);
            ret_value = 1;
            break;
            
        case 0xBB:
            LOG_INF("Request GoPro NAME");
            can_reply(0xFF,gopro_state.name,strlen(gopro_state.name));
            ret_value = 1;
            break;

        default:
            break;
        }
    }

    return ret_value;
}