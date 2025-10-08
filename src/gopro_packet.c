#include "gopro_packet.h"
#include <zephyr/logging/log.h>

#include "gopro_protobuf.h"
#include "gopro_client.h"
#include "leds.h"

K_SEM_DEFINE(get_hw_sem, 0, 1);
LOG_MODULE_REGISTER(gopro_packet, LOG_LEVEL_DBG);

struct gopro_packet_t gopro_packet;

extern struct gopro_state_t gopro_state;

static int gopro_parse_query_status_reply(const void *data, uint16_t length);

static void gopro_packet_parse_cmd(struct gopro_packet_t *gopro_packet);
static void gopro_packet_parse_query(struct gopro_packet_t *gopro_packet);
static void gopro_packet_parse_net(struct gopro_packet_t *gopro_packet);

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


void gopro_packet_get_pkt_ptr(const struct gopro_cmd_t *gopro_cmd, uint8_t *index, uint32_t *len){

    gopro_packet_type_t packet_type = gopro_packet_get_type(gopro_cmd);

    switch (packet_type)
    {
    case gopro_packet_5bit:
        *index = 1;
        *len = gopro_cmd->len - 1;
        break;

    case gopro_packet_13bit:
        *index = 2;
        *len = gopro_cmd->len - 2;
        LOG_DBG("13bit, index: %d len: %d",*index,*len);

        break;
    
    case gopro_packet_16bit:
        *index = 3;
        *len = gopro_cmd->len - 3;
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

static int gopro_parse_query_status_reply(const void *data, uint16_t length){
	uint8_t *pdata = (uint8_t *)data;
	int total_data_len;
	int result;
	int id;

	total_data_len = length;
	id = pdata[0];
	result = pdata[1];

	uint8_t status_id = pdata[2];
	uint8_t status_len = pdata[3];

	switch (status_id)
	{
	case GOPRO_STATUS_ID_VIDEO_NUM:
		gopro_state.video_count = 0;
		for(uint32_t i=0; i<status_len; i++){
			gopro_state.video_count = (gopro_state.video_count*256) + pdata[4+i];
		}	
		LOG_DBG("Video count: %d",gopro_state.video_count);
		break;

	case GOPRO_STATUS_ID_BAT_PERCENT:
		gopro_state.battery = pdata[4];
		LOG_DBG("Battery: %d",gopro_state.battery);
		break;

	case GOPRO_STATUS_ID_ENCODING:
		gopro_state.record = pdata[4];
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

static int gopro_parse_query_status_notify(const void *data, uint16_t length){
	uint8_t *pdata = (uint8_t *)data;
	int total_data_len;
	int result;
	int id;
	int id_len;

	total_data_len = length;

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


void gopro_packet_build(struct gopro_cmd_t *gopro_cmd){

    gopro_packet_type_t packet_type = gopro_packet_get_type(gopro_cmd);

    LOG_HEXDUMP_DBG(gopro_cmd->data,gopro_cmd->len,"INPUT DATA");

    if(packet_type == gopro_packet_cont){
        uint8_t packet_num = gopro_cmd->data[0] & 0x0F;
		LOG_DBG("Continuation Packet number %d for feature 0x%0X action 0x%0X",packet_num,gopro_packet.feature,gopro_packet.action);

        gopro_packet_get_data_ptr(gopro_cmd,&gopro_packet.data_start_index,&gopro_packet.data_len);

        if(gopro_packet.data == NULL){
            LOG_ERR("No memory ptr");
            return;
        }
        
        if((gopro_packet.data_len+gopro_packet.saved_len) > gopro_packet.total_len){
            LOG_ERR("Data size overflow, %d bytes of %d",(gopro_packet.data_len+gopro_packet.saved_len),gopro_packet.total_len);
            k_free(gopro_packet.data);
            gopro_packet.data = 0;
            return;
        }

        memcpy(&gopro_packet.data[gopro_packet.saved_len],&gopro_cmd->data[gopro_packet.data_start_index],gopro_packet.data_len);

        gopro_packet.saved_len += gopro_packet.data_len;

        if(gopro_packet.saved_len == gopro_packet.total_len){
            LOG_DBG("Full multi-packet saved");
            //LOG_HEXDUMP_DBG(gopro_packet.data,gopro_packet.total_len,"Total packet");
            gopro_packet_parse(&gopro_packet);
            k_free(gopro_packet.data);
            gopro_packet.data = 0;
        }
 
    }else{

        if(gopro_packet.data != NULL){
            LOG_WRN("Mem not free");
            k_free(gopro_packet.data);
        }
        
        memset(&gopro_packet,0,sizeof(struct gopro_packet_t));
        gopro_packet.packet_type = gopro_cmd->cmd_type;
        gopro_packet_get_feature(gopro_cmd,&gopro_packet.feature,&gopro_packet.action);
        gopro_packet_get_data_ptr(gopro_cmd,&gopro_packet.data_start_index,&gopro_packet.data_len);
        gopro_packet_get_pkt_ptr(gopro_cmd,&gopro_packet.pkt_start_index,&gopro_packet.pkt_len);
        gopro_packet.total_len = gopro_packet_get_len(gopro_cmd);
        gopro_packet.packet_len = gopro_packet.total_len-2; // Feature, action

        if(gopro_packet.total_len == 0){
            LOG_ERR("Total len = 0, Skip packet");
            return;
        }
        
        gopro_packet.data = k_malloc(gopro_packet.total_len);
        
        if(gopro_packet.data == NULL){
            LOG_ERR("Can't allocate %d bytes",gopro_packet.total_len);
            return;
        }else{
            LOG_DBG("Allocated %d bytes done",gopro_packet.total_len);
        }

        memset(gopro_packet.data,0,gopro_packet.total_len);

        LOG_DBG("Copy %d bytes ",gopro_packet.pkt_len);
        memcpy(gopro_packet.data,&gopro_cmd->data[gopro_packet.pkt_start_index],gopro_packet.pkt_len);

        gopro_packet.saved_len = gopro_packet.pkt_len;

        if(gopro_packet.saved_len == gopro_packet.total_len){
            LOG_DBG("Full single-packet saved");
            //LOG_HEXDUMP_DBG(gopro_packet.data,gopro_packet.total_len,"Total packet");
            gopro_packet_parse(&gopro_packet);
            k_free(gopro_packet.data);
            gopro_packet.data = 0;
        }
    }
}


void gopro_packet_parse(struct gopro_packet_t *gopro_packet){

    switch (gopro_packet->packet_type){

        case GP_CNTRL_HANDLE_CMD:
            LOG_DBG("Parse CMD packet");
            gopro_packet_parse_cmd(gopro_packet);
            break;

        case GP_CNTRL_HANDLE_SETTINGS:
            LOG_DBG("Parse SETTINGS packet");
            break;

        case GP_CNTRL_HANDLE_QUERY:
            gopro_packet_parse_query(gopro_packet);
            LOG_DBG("Parse QUERY packet");
            break;

        case GP_CNTRL_HANDLE_NET:
            gopro_packet_parse_net(gopro_packet);
            LOG_DBG("Parse NET packet");
            break;

        default:
            LOG_WRN("Unknown packet type: %d",gopro_packet->packet_type);
            break;
        }
}

static void gopro_parse_response_hw_info(struct gopro_packet_t *gopro_packet){
    uint8_t *pdata = &gopro_packet->data[2];
    uint32_t len = gopro_packet->packet_len;
    uint32_t index = 0;
    // char model_name[20];
    // char firmware_version[20];
    // char serial_number[20];
    // char ap_ssid[20];
    // char ap_mac[20];
    
    //LOG_HEXDUMP_DBG(gopro_packet->data,gopro_packet->total_len,"Parse CMD INPUT");

    uint8_t model_number_length = pdata[0];

    if(model_number_length > 4){
        LOG_ERR("Invalid model number len");
        return;
    }
    index++;

    uint32_t model_number = 0;
    for(uint32_t i=model_number_length; i>0; i--){
        model_number |= (uint32_t)(pdata[index] << (i-1)*8);
        index++;
    }
    LOG_DBG("Model number: 0x%0X",model_number);

    uint8_t model_name_length = pdata[index];
    index++;
    
    if(model_name_length >= sizeof(gopro_state.model_name)){
        LOG_ERR("Invalid model name len");
        return;
    }
    memcpy(gopro_state.model_name,&pdata[index],model_name_length);
    gopro_state.model_name[model_name_length]=0;
    index += model_name_length;
    LOG_DBG("Model name: %s",gopro_state.model_name);

    uint8_t deprecated_length = pdata[index];
    index++;
    index += deprecated_length;

    //FW Version
    uint8_t firmware_version_length = pdata[index];
    index++;
    if(firmware_version_length >= sizeof(gopro_state.firmware_version)){
        LOG_ERR("Invalid firmware_version len");
        return;
    }
    memcpy(gopro_state.firmware_version,&pdata[index],firmware_version_length);
    gopro_state.firmware_version[firmware_version_length]=0;
    index += firmware_version_length;
    LOG_DBG("Firmware: %s",gopro_state.firmware_version);

    //Serial Number
    uint8_t serial_number_length = pdata[index];
    index++;
    if(serial_number_length >= sizeof(gopro_state.serial_number)){
        LOG_ERR("Invalid serial_number len");
        return;
    }
    memcpy(gopro_state.serial_number,&pdata[index],serial_number_length);
    gopro_state.serial_number[serial_number_length]=0;
    index += serial_number_length;
    LOG_DBG("Serial: %s",gopro_state.serial_number);

    //AP SSID
    uint8_t ap_ssid_length = pdata[index];
    index++;
    if(ap_ssid_length >= sizeof(gopro_state.wifi_ssid)){
        LOG_ERR("Invalid ap_ssid len");
        return;
    }
    memcpy(gopro_state.wifi_ssid,&pdata[index],ap_ssid_length);
    gopro_state.wifi_ssid[ap_ssid_length]=0;
    index += ap_ssid_length;
    LOG_DBG("AP SSID: %s",gopro_state.wifi_ssid);

    //AP MAC
    uint8_t ap_mac_address_length = pdata[index];
    index++;
    if(ap_mac_address_length >= sizeof(gopro_state.ap_mac)){
        LOG_ERR("Invalid ap_mac_address_length len");
        return;
    }
    memcpy(gopro_state.ap_mac,&pdata[index],ap_mac_address_length);
    gopro_state.ap_mac[ap_mac_address_length]=0;
    index += ap_mac_address_length;
    LOG_DBG("AP MAC: %s",gopro_state.ap_mac);

    index += 11; //reserved data not part of the payload
    if(index == len){
        LOG_DBG("Packet parse correct");
    }else{
        LOG_WRN("Packet len error: %d %d",index,len);
    }
};


static void gopro_packet_parse_cmd(struct gopro_packet_t *gopro_packet){

    if(gopro_packet->feature == 0xF1){
        switch (gopro_packet->action)
        {
            case 0xE4:
            case 0xE5:
            case 0xE6:
            case 0xE7:
            case 0xE9:
            case 0xEB:
            case 0xF9:
                LOG_DBG("Generic response");
                gopro_parse_response_generic(&gopro_packet->data[2],gopro_packet->packet_len);
                break;

            default:
                LOG_WRN("Unknown CMD action 0xF1:0x%0X",gopro_packet->action);
                break;
        }
    }

    if(gopro_packet->feature == 0x3C){
        LOG_DBG("Get HW status response");
        switch (gopro_packet->action)
        {
        case 0:
            LOG_DBG("Status OK");
            gopro_parse_response_hw_info(gopro_packet);
            k_sem_give(&get_hw_sem);
            break;
        case 1:
            LOG_ERR("Status Error");
            break;
        case 2:
            LOG_ERR("Invalid Parameter");
            break;
        
        default:
            LOG_ERR("Unknown status: %d",gopro_packet->action);
            break;
        }
    
    }
}

static void gopro_packet_parse_query(struct gopro_packet_t *gopro_packet){

    if(gopro_packet->feature == 0xF5){
        switch (gopro_packet->action){
        case 0xEF:
            gopro_parse_response_cohn_status(&gopro_packet->data[2],gopro_packet->packet_len);
            break;

        case 0xEE:
            gopro_parse_response_cohn_cert(&gopro_packet->data[2],gopro_packet->packet_len);
            break;
            
        default:
            LOG_WRN("Unknown QUERY action 0xF5:0x%0X",gopro_packet->action);
            break;
        }  
    };

    if( (gopro_packet->feature == GOPRO_QUERY_STATUS_REG_STATUS)||(gopro_packet->feature == GOPRO_QUERY_STATUS_REG_STATUS_NOTIFY)){
        if(gopro_packet->action == 0){
            gopro_parse_query_status_notify(gopro_packet->data, gopro_packet->total_len);
        }else{
            LOG_ERR("REG Result not OK: %d",gopro_packet->action);
        }	
    }

    if(gopro_packet->feature == GOPRO_QUERY_STATUS_GET_STATUS){
        if(gopro_packet->action == 0){
            gopro_parse_query_status_reply(gopro_packet->data, gopro_packet->total_len);
        }else{
            LOG_ERR("REG Result not OK: %d",gopro_packet->action);
        }	
    }
}

static void gopro_packet_parse_net(struct gopro_packet_t *gopro_packet){

    if(gopro_packet->feature == 0x02){ //Feature ID
        switch (gopro_packet->action){ //Action ID
            case 0x0B:
                LOG_DBG("NotifStartScanning");
                gopro_parse_start_scaning(&gopro_packet->data[2],gopro_packet->packet_len);
                break;

            case 0x82:
                LOG_DBG("ResponseStartScanning");
                gopro_parse_request_scan_req(&gopro_packet->data[2],gopro_packet->packet_len);
                break;

            case 0x83:
                LOG_DBG("ResponseGetApEntries");
                gopro_parse_ap_entries(gopro_packet);
                break;

            case 0x84:
                LOG_DBG("ResponseConnect");
                gopro_parse_resp_connect(&gopro_packet->data[2],gopro_packet->packet_len);
                break;

            case 0x85:
                LOG_DBG("ResponseConnectNew");
                gopro_parse_resp_connect_new(&gopro_packet->data[2],gopro_packet->packet_len);
                break;

            case 0x0C:
                LOG_DBG("NotifProvisioningState");
                gopro_parse_notif_prov_state(&gopro_packet->data[2],gopro_packet->packet_len);
                break;

            default:
                LOG_WRN("No PARSE for 0x02:0x%0X",gopro_packet->action);
                break;
        }
    }


    if(gopro_packet->feature == 0xF1){ //Feature ID
    switch (gopro_packet->action){ //Action ID

        case 0xE6:
            LOG_DBG("Clear COHN Certificate response");
            gopro_parse_response_generic(&gopro_packet->data[2],gopro_packet->packet_len);
            break;

        case 0xE7:
            LOG_DBG("Create COHN Certificate response");
            gopro_parse_response_generic(&gopro_packet->data[2],gopro_packet->packet_len);
            break;

        default:
            LOG_WRN("No PARSE for 0xF1:0x%0X",gopro_packet->action);
            break;
        
    }
}

    if(gopro_packet->feature == 0x03){ //Feature ID
    switch (gopro_packet->action){ //Action ID

        case 0x81:
            LOG_DBG("Set Pairing State response");
            gopro_parse_response_generic(&gopro_packet->data[2],gopro_packet->packet_len);
            break;


        default:
            LOG_WRN("No PARSE for 0x03:0x%0X",gopro_packet->action);
            break;
        
    }
}

}