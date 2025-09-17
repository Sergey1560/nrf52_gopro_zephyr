#include "gopro_packet.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gopro_packet, LOG_LEVEL_DBG);


gopro_packet_type_t gopro_packet_get_type(const struct gopro_cmd_t *gopro_cmd){

    gopro_packet_type_t ret_val;

	if(gopro_cmd->data[0] & 0x80){
		ret_val = gopro_packet_cont;
	}else{
		uint8_t packet_type = ((gopro_cmd->data[0] & 0x60) >> 5);

        if(packet_type > gopro_packet_16bit){
            ret_val = gopro_packet_invalid;
        }else{
            ret_val = packet_type;
        }
    };
    return ret_val;
}

int gopro_packet_get_len(const struct gopro_cmd_t *gopro_cmd){
    gopro_packet_type_t packet_type = gopro_packet_get_type(gopro_cmd);
    int packet_data_len = -1;

    switch (packet_type)
		{
		case gopro_packet_5bit:
			packet_data_len = (gopro_cmd->data[0] & 0x1F);
			break;

		case gopro_packet_13bit:
			packet_data_len = ((gopro_cmd->data[0] & 0x1F) << 8)|gopro_cmd->data[1];
			break;

		case gopro_packet_16bit:
			packet_data_len = (gopro_cmd->data[1] << 8)|gopro_cmd->data[2];
			break;
		
		default:
			break;
		}

        return packet_data_len;
}

void gopro_packet_get_feature(const struct gopro_cmd_t *gopro_cmd, uint8_t *feature, uint8_t *action){

    gopro_packet_type_t packet_type = gopro_packet_get_type(gopro_cmd);

    switch (packet_type)
    {
    case gopro_packet_5bit:
        *feature = gopro_cmd->data[1];
        *action = gopro_cmd->data[2];
        break;
    case gopro_packet_13bit:
        *feature = gopro_cmd->data[2];
        *action = gopro_cmd->data[3];
        break;
    case gopro_packet_16bit:
        *feature = gopro_cmd->data[3];
        *action = gopro_cmd->data[4];
        break;

        
    default:
        break;
    }
}

void gopro_packet_get_data_ptr(const struct gopro_cmd_t *gopro_cmd, uint8_t *index, uint32_t *len){

    gopro_packet_type_t packet_type = gopro_packet_get_type(gopro_cmd);

    switch (packet_type)
    {
    case gopro_packet_5bit:
        *index = 3;
        *len = gopro_cmd->len - 3;
        LOG_DBG("5bit, index: %d len: %d", *index,*len);

        break;

    case gopro_packet_13bit:
        *index = 4;
        *len = gopro_cmd->len - 4;
        LOG_DBG("13bit, index: %d len: %d",*index,*len);

        break;
    
    case gopro_packet_16bit:
        *index = 5;
        *len = gopro_cmd->len - 5;
        LOG_DBG("16bit, index: %d len: %d",*index,*len);

        break;

    case gopro_packet_cont:
        *index = 1;
        *len = gopro_cmd->len - 1;
        LOG_DBG("Cont, index: %d len: %d",*index,*len);

        break;

    default:
        *index = 0;
        *len = gopro_cmd->len;
        LOG_DBG("UNK, index: %d len: %d",*index,*len);
        break;
    }

};