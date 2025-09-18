#include "gopro_protobuf.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "src/protobuf/cohn.pb.h"
#include "src/protobuf/live_streaming.pb.h"
#include "src/protobuf/media.pb.h"
#include "src/protobuf/network_management.pb.h"
#include "src/protobuf/preset_status.pb.h"
#include "src/protobuf/request_get_preset_status.pb.h"
#include "src/protobuf/response_generic.pb.h"
#include "src/protobuf/set_camera_control_status.pb.h"
#include "src/protobuf/turbo_transfer.pb.h"

#include <pb_encode.h>
#include <pb_decode.h>

#include "gopro_packet.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gopro_protobuf, LOG_LEVEL_DBG);

static void gopro_parse_start_scaning(uint8_t *data, uint32_t len);
static void gopro_parse_request_scan_req(uint8_t *data, uint32_t len);

static char *gopro_pb_provstate(open_gopro_EnumProvisioning state);
static void gopro_parse_notif_prov_state(uint8_t *data, uint32_t len);

static void gopro_parse_resp_connect_new(uint8_t *data, uint32_t len);
static void gopro_parse_resp_connect(uint8_t *data, uint32_t len);

static void gopro_parse_response_cohn_status(uint8_t *data, uint32_t len);

static int gopro_pb_req_ap(uint8_t scan_id, uint8_t start_index, uint8_t count);

static char *gopro_pb_result(open_gopro_EnumResultGeneric state);
static char *gopro_pb_state(open_gopro_EnumScanning state);

static char *gopro_pb_cohn_status(open_gopro_EnumCOHNStatus state);
static char *gopro_pb_cohn_state(open_gopro_EnumCOHNNetworkState state);

static int gopro_parse_ap_entries(struct ap_entries_desc_t *ap_entries_desc, uint8_t *data, uint32_t len);
static void gopro_connect_ap(struct ap_list_t *ap_list, int count);
static uint32_t gopro_prepare_connect_new(uint8_t *data, uint32_t max_len);
static uint32_t gopro_prepare_connect_saved(uint8_t *data, uint32_t max_len);

static void gopro_send_big_data(uint8_t *data, uint32_t len, uint8_t type, uint8_t feature, uint8_t action);

ZBUS_CHAN_DECLARE(gopro_cmd_chan);

const char *pb_enum_result[8]={
    "(0)RESULT_UNKNOWN",
    "(1)RESULT_SUCCESS",
    "(2)RESULT_ILL_FORMED",
    "(3)RESULT_NOT_SUPPORTED",
    "(4)RESULT_ARGUMENT_OUT_OF_BOUNDS",
    "(5)RESULT_ARGUMENT_INVALID",
    "(6)RESULT_RESOURCE_NOT_AVAILABLE",
    "ERROR_INDEX"
};

const char *pb_enum_state[7]={
    "(0)SCANNING_UNKNOWN ",
    "(1)SCANNING_NEVER_STARTED",
    "(2)SCANNING_STARTED",
    "(3)SCANNING_ABORTED_BY_SYSTEM",
    "(4)SCANNING_CANCELLED_BY_USER",
    "(5)SCANNING_SUCCESS",
    "ERROR_INDEX"
};

const char *pb_enum_prov_state[13]={
    "(0)PROVISIONING_UNKNOWN",
    "(1)PROVISIONING_NEVER_STARTED",
    "(2)PROVISIONING_STARTED",
    "(3)PROVISIONING_ABORTED_BY_SYSTEM",
    "(4)PROVISIONING_CANCELLED_BY_USER",
    "(5)PROVISIONING_SUCCESS_NEW_AP",
    "(6)PROVISIONING_SUCCESS_OLD_AP",
    "(7)PROVISIONING_ERROR_FAILED_TO_ASSOCIATE",
    "(8)PROVISIONING_ERROR_PASSWORD_AUTH",
    "(9)PROVISIONING_ERROR_EULA_BLOCKING",
    "(10)PROVISIONING_ERROR_NO_INTERNET ",
    "(11)PROVISIONING_ERROR_UNSUPPORTED_TYPE",
    "ERROR_INDEX"
};

const char *pb_enum_cohn_status[3]={
    "(0)COHN_UNPROVISIONED",
    "(1)COHN_PROVISIONED",
    "ERROR_INDEX"
};

const char *pb_enum_cohn_netstate[9]={
    "(0)COHN_STATE_Init",
    "(1)COHN_STATE_Error",
    "(2)COHN_STATE_Exit",
    "(5)COHN_STATE_Idle",
    "(27)COHN_STATE_NetworkConnected",
    "(28)COHN_STATE_NetworkDisconnected",
    "(29)COHN_STATE_ConnectingToNetwork",
    "(30)COHN_STATE_Invalid",
    "ERROR_INDEX"
};

//const uint8_t gopro_RequestGetCOHNStatus[]={0x04,0xF5,0x6F,0x8,0x1}; 

// const char my_wifi_ssid[]="Dlink602_1";
// const char my_wifi_pass[]="Hnt45vh4JySqi";
const char my_wifi_ssid[]="service_5";
const char my_wifi_pass[]=".W@71<5KD1J\"uz";

uint8_t work_buff[WORK_BUFF_SIZE];

struct ap_entries_desc_t ap_entries_desc;

volatile int ap_list_index = 0;
volatile struct ap_list_t ap_list[MAX_AP_LIST_COUNT];

uint8_t resp_ap_entries_buf[128];

int gopro_parse_net_reply(struct gopro_cmd_t *gopro_cmd){
    static uint8_t last_action = 0;
    static uint8_t last_feature = 0;
    uint8_t feature;
    uint8_t action;
    uint8_t packet_num;
    uint32_t len;
    uint8_t index;
    LOG_DBG("Parse NET reply");

    gopro_packet_type_t packet_type = gopro_packet_get_type(gopro_cmd);
    
	if(packet_type == gopro_packet_cont){
        packet_num = gopro_cmd->data[0] & 0x0F;
		LOG_DBG("NET Continuation Packet number %d for feature 0x%0X action 0x%0X",packet_num,last_feature,last_action);

        if(last_feature == 0x02){
            switch (last_action){
            case 0x83:
                gopro_packet_get_data_ptr(gopro_cmd,&index,&len);
                gopro_parse_ap_entries(&ap_entries_desc,&gopro_cmd->data[index],len);
                break;
            
            default:
                break;
            }
        }

    }else{
        gopro_packet_get_feature(gopro_cmd,&feature,&action);
        gopro_packet_get_data_ptr(gopro_cmd,&index,&len);

        last_feature = feature;
        last_action = action;

        if(feature == 0x02){ //Feature ID
            switch (action){ //Action ID
                case 0x0B:
                    LOG_DBG("NotifStartScanning");
                    gopro_parse_start_scaning(&gopro_cmd->data[index],len);
                    break;

                case 0x82:
                    LOG_DBG("ResponseStartScanning");
                    gopro_parse_request_scan_req(&gopro_cmd->data[index],len);
                    break;

                case 0x83:
                    LOG_DBG("ResponseGetApEntries");
                    memset(work_buff,0,WORK_BUFF_SIZE);
                    memset((uint8_t *)&ap_list,0,sizeof(ap_list));
                    ap_entries_desc.packet_data_len = gopro_packet_get_len(gopro_cmd)-2;
                    ap_entries_desc.packet_num = 0;
                    ap_entries_desc.saved_bytes = 0;
                    ap_list_index=0;
                    LOG_DBG("Start AP entires for %d bytes",ap_entries_desc.packet_data_len);
                    gopro_parse_ap_entries(&ap_entries_desc,&gopro_cmd->data[index],len);
                    break;

                case 0x84:
                    LOG_DBG("ResponseConnect");
                    gopro_parse_resp_connect(&gopro_cmd->data[index],len);
                    break;

                case 0x85:
                    LOG_DBG("ResponseConnectNew");
                    gopro_parse_resp_connect_new(&gopro_cmd->data[index],len);
                    break;

                case 0x0C:
                    LOG_DBG("NotifProvisioningState");
                    gopro_parse_notif_prov_state(&gopro_cmd->data[index],len);
                    break;

                default:
                    LOG_WRN("No PARSE for feature 0x02 Action ID 0x%0X",action);
                    break;
            }
        }
        if(feature == 0xF1){ //Feature ID
            switch (action){ //Action ID

                case 0xE6:
                    LOG_DBG("Clear COHN Certificate response");
                    gopro_parse_response_generic(&gopro_cmd->data[index],len);
                    break;

                case 0xE7:
                    LOG_DBG("Create COHN Certificate response");
                    gopro_parse_response_generic(&gopro_cmd->data[index],len);
                    break;

                default:
                    LOG_WRN("No PARSE for feature 0xF1 Action ID 0x%0X",action);
                    break;
                
            }
        }

    }
    return 0;
}

bool read_str_callback(pb_istream_t *stream, const pb_field_t *field, void **arg) {
    char buffer[64];
    int bytes = stream->bytes_left;
    char *pattern = *arg;

    if(bytes >= sizeof(buffer)){
        LOG_ERR("Str len > buffer size %d of %d",bytes,sizeof(buffer));
        return false;
    }

    if (pb_read(stream, (pb_byte_t *)buffer, stream->bytes_left)) {
        buffer[bytes] = '\0'; // Null-terminate the string
        LOG_DBG("%s %s",pattern,buffer);
        return true;
    }

    return false; // Failed to read the string
}


static void gopro_parse_response_cohn_status(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

    open_gopro_NotifyCOHNStatus scan_resp = open_gopro_NotifyCOHNStatus_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    scan_resp.ipaddress.funcs.decode = read_str_callback;
    scan_resp.ipaddress.arg = "IP:";

    scan_resp.macaddress.funcs.decode = read_str_callback;
    scan_resp.macaddress.arg = "MAC:";

    scan_resp.ssid.funcs.decode = read_str_callback;
    scan_resp.ssid.arg = "SSID:";

    scan_resp.username.funcs.decode = read_str_callback;
    scan_resp.username.arg = "User:";

    scan_resp.password.funcs.decode = read_str_callback;
    scan_resp.password.arg = "Pass:";

    err = pb_decode(&stream, open_gopro_NotifyCOHNStatus_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(input_data,input_len,"Data:");
    }else{
        if(scan_resp.has_state){
            LOG_DBG("State: %s",gopro_pb_cohn_state(scan_resp.state));    
        }

        if(scan_resp.has_status){
            LOG_DBG("Status: %s State: %s",gopro_pb_cohn_status(scan_resp.status),gopro_pb_cohn_state(scan_resp.state));
        }

        if(scan_resp.has_enabled){
            LOG_DBG("COHN Enabled: %d",scan_resp.enabled);
        }
    }

};

static bool cert_decode_callback(pb_istream_t *stream, const pb_field_t *field, void **arg) {
    int bytes = stream->bytes_left;
    char *buffer = *arg;

    if (pb_read(stream, (pb_byte_t *)buffer, stream->bytes_left)) {
        LOG_DBG("Cert size: %d",bytes);
        return true;
    }

    return false; // Failed to read the string
}


static void gopro_parse_response_cohn_cert(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

    open_gopro_ResponseCOHNCert scan_resp = open_gopro_ResponseCOHNCert_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    scan_resp.cert.funcs.decode=cert_decode_callback;
    scan_resp.cert.arg = work_buff;

    err = pb_decode(&stream, open_gopro_ResponseCOHNCert_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(input_data,input_len,"Data:");
    }else{
        if(scan_resp.has_result){
            LOG_DBG("Result: %s",gopro_pb_result(scan_resp.result));    
        }
    }

};

static void gopro_parse_resp_connect_new(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

    open_gopro_ResponseConnectNew scan_resp = open_gopro_ResponseConnectNew_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    err = pb_decode(&stream, open_gopro_ResponseConnectNew_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(input_data,input_len,"Data:");
    }else{
        LOG_DBG("Result: %s State: %s",gopro_pb_result(scan_resp.result),gopro_pb_provstate(scan_resp.provisioning_state));
    }
}

static void gopro_parse_resp_connect(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

    open_gopro_ResponseConnect scan_resp = open_gopro_ResponseConnect_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    err = pb_decode(&stream, open_gopro_ResponseConnect_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(input_data,input_len,"Data:");
    }else{
        LOG_DBG("Result: %s State: %s",gopro_pb_result(scan_resp.result),gopro_pb_provstate(scan_resp.provisioning_state));
    }
};


static void gopro_parse_notif_prov_state(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

    open_gopro_NotifProvisioningState scan_resp = open_gopro_NotifProvisioningState_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    err = pb_decode(&stream, open_gopro_NotifProvisioningState_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(input_data,input_len,"Data:");
    }else{
        LOG_DBG("State: %s",gopro_pb_provstate(scan_resp.provisioning_state));
    }
}


static void gopro_parse_request_scan_req(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

    open_gopro_ResponseStartScanning scan_resp = open_gopro_ResponseStartScanning_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    err = pb_decode(&stream, open_gopro_ResponseStartScanning_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(input_data,input_len,"Data:");
    }else{
        LOG_DBG("Result: %s State: %s",gopro_pb_result(scan_resp.result),gopro_pb_state(scan_resp.scanning_state));
    }
}

void gopro_parse_response_generic(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

    open_gopro_ResponseGeneric scan_resp = open_gopro_ResponseGeneric_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    err = pb_decode(&stream, open_gopro_ResponseGeneric_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(input_data,input_len,"Data:");
    }else{
        LOG_DBG("Result: %s",gopro_pb_result(scan_resp.result));
    }
}

static void gopro_parse_start_scaning(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

    open_gopro_NotifStartScanning scan_resp = open_gopro_NotifStartScanning_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    err = pb_decode(&stream, open_gopro_NotifStartScanning_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(input_data,input_len,"Data:");
    }else{
        LOG_DBG("Scan_id: %d  Totlal: %d  State: %s",scan_resp.scan_id, scan_resp.total_entries, gopro_pb_state(scan_resp.scanning_state));
    }

    if(scan_resp.scanning_state == open_gopro_EnumScanning_SCANNING_SUCCESS){
        gopro_pb_req_ap(scan_resp.scan_id,0,scan_resp.total_entries);
    }
}

static char *gopro_pb_result(open_gopro_EnumResultGeneric state){
   
    if((state>=0) && (state < 8)){
        return (char *)pb_enum_result[state];
      }else{
        return (char *)pb_enum_result[7];
      }

}

static char *gopro_pb_state(open_gopro_EnumScanning state){
   
    if((state>=0) && (state < 7)){
        return (char *)pb_enum_state[state];
      }else{
        return (char *)pb_enum_state[6];
      }

}

static char *gopro_pb_provstate(open_gopro_EnumProvisioning state){
   
    if((state>=0) && (state < 12)){
        return (char *)pb_enum_prov_state[state];
      }else{
        return (char *)pb_enum_prov_state[6];
      }

}

static char *gopro_pb_cohn_status(open_gopro_EnumCOHNStatus state){
   
    if((state>=0) && (state < 2)){
        return (char *)pb_enum_cohn_status[state];
      }else{
        return (char *)pb_enum_cohn_status[7];
      }

}

static char *gopro_pb_cohn_state(open_gopro_EnumCOHNNetworkState state){
   
    switch (state)
    {
    case open_gopro_EnumCOHNNetworkState_COHN_STATE_Init:
        return (char *)pb_enum_cohn_netstate[0];
        break;

    case open_gopro_EnumCOHNNetworkState_COHN_STATE_Error:
        return (char *)pb_enum_cohn_netstate[1];
        break;

    case open_gopro_EnumCOHNNetworkState_COHN_STATE_Exit:
        return (char *)pb_enum_cohn_netstate[2];
        break;

    case open_gopro_EnumCOHNNetworkState_COHN_STATE_Idle:
        return (char *)pb_enum_cohn_netstate[3];
        break;

    case open_gopro_EnumCOHNNetworkState_COHN_STATE_NetworkConnected:
        return (char *)pb_enum_cohn_netstate[4];
        break;

    case open_gopro_EnumCOHNNetworkState_COHN_STATE_NetworkDisconnected:
        return (char *)pb_enum_cohn_netstate[5];
        break;

    case open_gopro_EnumCOHNNetworkState_COHN_STATE_ConnectingToNetwork:
        return (char *)pb_enum_cohn_netstate[6];
        break;

    case open_gopro_EnumCOHNNetworkState_COHN_STATE_Invalid:
        return (char *)pb_enum_cohn_netstate[7];
        break;

    default:
        return (char *)pb_enum_cohn_netstate[8];
        break;
    }
}

static int gopro_pb_req_ap(uint8_t scan_id, uint8_t start_index, uint8_t count){
    int err;
    struct gopro_cmd_t gopro_cmd;

    LOG_DBG("Request AP from index %d, count %d",start_index, count);

    gopro_cmd.data[0] = 0;
    gopro_cmd.data[1] = 0x02; //Feature ID
    gopro_cmd.data[2] = 0x03; //Feature ID

    open_gopro_RequestGetApEntries req = open_gopro_RequestGetApEntries_init_zero;

    req.scan_id=scan_id;
    req.start_index=0;
    req.max_entries=count;

    pb_ostream_t out_stream = pb_ostream_from_buffer(&gopro_cmd.data[3], GOPRO_CMD_DATA_LEN-3);
    err = pb_encode(&out_stream, open_gopro_RequestGetApEntries_fields, &req);

    if(!err){
        LOG_ERR("Encode failed");
        return -1;
    }else{
        gopro_cmd.cmd_type = GP_CNTRL_HANDLE_NET;        
        gopro_cmd.data[0] = out_stream.bytes_written+2;
        gopro_cmd.len = gopro_cmd.data[0]+1;

        err = zbus_chan_pub(&gopro_cmd_chan, &gopro_cmd, K_NO_WAIT);
        if(err != 0){
            if(err == -ENOMSG){
                LOG_ERR("Invalid Gopro state, skip cmd");
            }else{
                LOG_ERR("Chan pub failed: %d",err);
            }
            return -1;
        }
    }

    return 0;
}

bool read_ssid_callback(pb_istream_t *stream, const pb_field_t *field, void **arg) {
    struct ap_list_t *ap_list_ptr = (struct ap_list_t *)*arg;
    int bytes = stream->bytes_left;

    if(bytes > MAX_SSID_LEN){
        LOG_ERR("Str len > buffer size %d of %d",bytes,MAX_SSID_LEN);
        return false;
    }

    if (pb_read(stream, (pb_byte_t *)ap_list_ptr->ssid, stream->bytes_left)) {
        ap_list_ptr->ssid[bytes] = '\0'; // Null-terminate the string
        return true;
    }

    return false; // Failed to read the string
}

static bool read_ap_entries_callback(pb_istream_t *stream, const pb_field_t *field, void **arg) {
    int err;
    int bytes = stream->bytes_left;
    struct ap_list_t *ap_list_ptr = (struct ap_list_t *)*arg;

    if(bytes > sizeof(resp_ap_entries_buf)){
        LOG_ERR("Read bytes > entries size %d of %d",stream->bytes_left,sizeof(resp_ap_entries_buf));
        return false;
    }

    if (pb_read(stream, (pb_byte_t *)resp_ap_entries_buf, bytes)) {

        open_gopro_ResponseGetApEntries_ScanEntry resp = open_gopro_ResponseGetApEntries_ScanEntry_init_zero;
        resp.ssid.funcs.decode = read_ssid_callback;
        resp.ssid.arg = (struct ap_list_t*)&ap_list_ptr[ap_list_index];

        pb_istream_t istream = pb_istream_from_buffer((const pb_byte_t *)resp_ap_entries_buf, bytes);

        err = pb_decode(&istream, open_gopro_ResponseGetApEntries_ScanEntry_fields, &resp);
        if(!err){
            LOG_ERR("PB decode failed %s", PB_GET_ERROR(&istream));
        }else{
            ap_list_ptr[ap_list_index].signal_frequency_mhz = resp.signal_frequency_mhz;
            ap_list_ptr[ap_list_index].signal_strength_bars = resp.signal_strength_bars;
            ap_list_ptr[ap_list_index].flags = resp.scan_entry_flags;
            if(ap_list_index < (MAX_AP_LIST_COUNT-1)){
                ap_list_index++;
            }else{
                LOG_ERR("AP count overload %d",MAX_AP_LIST_COUNT);
            }
        }
    
    }else{
        LOG_ERR("Read failed");
    }

    return true;
}

static int gopro_parse_ap_entries(struct ap_entries_desc_t *ap_entries_desc, uint8_t *data, uint32_t len){
    int err;

    if((ap_entries_desc->saved_bytes + len) > WORK_BUFF_SIZE){
        LOG_ERR("Not enougth buffer for AP Entries: %d of %d",(ap_entries_desc->saved_bytes + len),WORK_BUFF_SIZE);
        return -1;
    }

    if((ap_entries_desc->saved_bytes + len) > ap_entries_desc->packet_data_len){
        LOG_ERR("Saved bytes > packet bytes %d of %d",(ap_entries_desc->saved_bytes + len),ap_entries_desc->packet_data_len);
        return -1;
    }

    memcpy(&work_buff[ap_entries_desc->saved_bytes],data,len);
    ap_entries_desc->saved_bytes+=len;

    LOG_DBG("AP Saved %d of %d",ap_entries_desc->saved_bytes,ap_entries_desc->packet_data_len);

    if(ap_entries_desc->saved_bytes == ap_entries_desc->packet_data_len){
        LOG_DBG("Full packet saved");

        open_gopro_ResponseGetApEntries resp = open_gopro_ResponseGetApEntries_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(work_buff, ap_entries_desc->packet_data_len);

        resp.entries.funcs.decode = read_ap_entries_callback;
        resp.entries.arg = (struct ap_list_t*)ap_list;

        err = pb_decode(&stream, open_gopro_ResponseGetApEntries_fields, &resp);

        if(!err){
            LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        }else{
            LOG_DBG("AP Decode OK.");
            gopro_connect_ap((struct ap_list_t *)ap_list, ap_list_index);
        }
    }

    LOG_DBG("AP parse finish");
    return 0;
};

static void gopro_connect_ap(struct ap_list_t *ap_list, int count){

    for(uint32_t i=0; i<count; i++){

        if( strncmp(ap_list[i].ssid,my_wifi_ssid,strlen(my_wifi_ssid)) == 0 ){
            if( (ap_list[i].flags & open_gopro_EnumScanEntryFlags_SCAN_FLAG_ASSOCIATED) > 0){
                LOG_DBG("Already connected to SSID %s",my_wifi_ssid);
                return;
            }

            if( (ap_list[i].flags & open_gopro_EnumScanEntryFlags_SCAN_FLAG_CONFIGURED) > 0){
                LOG_DBG("Connect to saved SSID %s",my_wifi_ssid);
                int len = gopro_prepare_connect_saved(work_buff,WORK_BUFF_SIZE);
                // LOG_DBG("Req size: %d",len);
                // LOG_HEXDUMP_DBG(work_buff,len,"PB Request:");
                gopro_send_big_data(work_buff,len,GP_CNTRL_HANDLE_NET,0x02,0x04);

            }else{
                LOG_DBG("Connect to new SSID %s",my_wifi_ssid);
                int len = gopro_prepare_connect_new(work_buff,WORK_BUFF_SIZE);
                // LOG_DBG("Req size: %d",len);
                // LOG_HEXDUMP_DBG(work_buff,len,"PB Request:");
                gopro_send_big_data(work_buff,len,GP_CNTRL_HANDLE_NET,0x02,0x05);
            }
            
            break;
        }
    }
}


bool copy_str(pb_ostream_t *stream, const pb_field_t *field, void *const *arg){

    const char* str = (const char*)(*arg);

    if (!pb_encode_tag_for_field(stream, field))
        return false;

    return pb_encode_string(stream, (uint8_t*)str, strlen(str));
}

static uint32_t gopro_prepare_connect_new(uint8_t *data, uint32_t max_len){
    int err;
    open_gopro_RequestConnectNew req = open_gopro_RequestConnectNew_init_zero;

    req.ssid.funcs.encode = copy_str;
    req.ssid.arg = (char *)my_wifi_ssid;

    req.password.funcs.encode = copy_str;
    req.password.arg = (char *)my_wifi_pass;

    pb_ostream_t stream = pb_ostream_from_buffer(data, max_len);

    err = pb_encode(&stream, open_gopro_RequestConnectNew_fields, &req);

    return stream.bytes_written;

}

static uint32_t gopro_prepare_connect_saved(uint8_t *data, uint32_t max_len){
    int err;
    open_gopro_RequestConnect req = open_gopro_RequestConnect_init_zero;

    req.ssid.funcs.encode = copy_str;
    req.ssid.arg = (char *)my_wifi_ssid;

    pb_ostream_t stream = pb_ostream_from_buffer(data, max_len);

    err = pb_encode(&stream, open_gopro_RequestConnect_fields, &req);

    return stream.bytes_written;
}

static void gopro_send_big_data(uint8_t *data, uint32_t len, uint8_t type, uint8_t feature, uint8_t action){
    int err;
    struct gopro_cmd_t gopro_cmd; 
    uint8_t *data_ptr;

    gopro_cmd.cmd_type = type;

    if(len <= (20 - 3)){ //5bit packet
        LOG_DBG("5bit packet Len: %d",len);
        gopro_cmd.len = len+3;
        gopro_cmd.data[0] = (len+2) & 0x1f;
        gopro_cmd.data[1] = feature;
        gopro_cmd.data[2] = action;
        for(uint32_t i=0; i<len; i++){
            gopro_cmd.data[3+i] = data[i];
        }
        LOG_HEXDUMP_DBG(gopro_cmd.data,gopro_cmd.len,"Packet:");
        err = zbus_chan_pub(&gopro_cmd_chan, &gopro_cmd, K_NO_WAIT);
		if(err != 0){
			if(err == -ENOMSG){
				LOG_ERR("Invalid Gopro state, skip cmd");
			}
			LOG_ERR("CMD chan pub failed: %d",err);
		}
    }else if(len < 8191){ //13bit
        LOG_DBG("13bit packet Len: %d",len);

        gopro_cmd.len = 20;
        gopro_cmd.data[0] = (((len+2) >> 8) & 0x1f) | (1 << 5);
        gopro_cmd.data[1] = (uint8_t)((len+2) & 0xff);
        gopro_cmd.data[2] = feature;
        gopro_cmd.data[3] = action;
        
        for(uint32_t i=0; i<17; i++){
            gopro_cmd.data[4+i] = data[i];
        }

        //LOG_HEXDUMP_DBG(gopro_cmd.data,gopro_cmd.len,"Packet:");
        err = zbus_chan_pub(&gopro_cmd_chan, &gopro_cmd, K_NO_WAIT);
		if(err != 0){
			if(err == -ENOMSG){
				LOG_ERR("Invalid Gopro state, skip cmd");
			}
			LOG_ERR("CMD chan pub failed: %d",err);
		}

        len = len - 16;
        data_ptr = data+16;
        uint8_t packet_num = 0;

        while(len > 0){
            gopro_cmd.data[0] = 0x80|(packet_num & 0x0F);

            uint8_t data_len = (len > 19) ? 19 : len;

            LOG_DBG("Cont packet len: %d datalen: %d",len,data_len);

            for(uint32_t i=0; i<data_len; i++){
                gopro_cmd.data[1+i]=data_ptr[i];
            }

            gopro_cmd.len = data_len+1;

            //LOG_HEXDUMP_DBG(gopro_cmd.data,gopro_cmd.len,"Packet:");
            err = zbus_chan_pub(&gopro_cmd_chan, &gopro_cmd, K_NO_WAIT);
            if(err != 0){
                if(err == -ENOMSG){
                    LOG_ERR("Invalid Gopro state, skip cmd");
                }
                LOG_ERR("CMD chan pub failed: %d",err);
            }
            
            packet_num++;
            
            len = len - data_len;
            data_ptr += data_len;
        }


    }else if(len < 65535){ //16bit
        //To do...
    }else{
        LOG_ERR("Packet to big: %d",len);
    }
}

/*
Первый пакет с значением в packet_len устанавливает значение и сбрасывает saved_bytes
Остальные пакеты с -1
*/
int gopro_build_packet_cohn_status(uint8_t *data, uint32_t len, int32_t packet_len){
    static int32_t packet_data_len = 0;
    static uint32_t saved_bytes = 0;

    LOG_DBG("BUILD COHN Status");
    if(packet_len != -1){
        packet_data_len = packet_len;
        saved_bytes = 0;
    }

    if((saved_bytes + len) > WORK_BUFF_SIZE){
        LOG_ERR("Not enougth buffer: %d of %d",(saved_bytes + len),WORK_BUFF_SIZE);
        return -1;
    }

    if((saved_bytes + len) > packet_data_len){
        LOG_ERR("Saved bytes > packet bytes %d of %d",(saved_bytes + len),packet_data_len);
        return -1;
    }

    memcpy(&work_buff[saved_bytes],data,len);
    saved_bytes+=len;

    LOG_DBG("Saved %d of %d",saved_bytes,packet_data_len);

    if(saved_bytes == packet_data_len){
        LOG_DBG("Full packet saved");

        gopro_parse_response_cohn_status(work_buff,saved_bytes);

        LOG_DBG("Packet parse finish");
    return 0;
    };
    return 0;
};

/*
Первый пакет с значением в packet_len устанавливает значение и сбрасывает saved_bytes
Остальные пакеты с -1
*/
int gopro_build_packet_cohn_cert(uint8_t *data, uint32_t len, int32_t packet_len){
    static int32_t packet_data_len = 0;
    static uint32_t saved_bytes = 0;

    LOG_DBG("BUILD COHN Cert");
    if(packet_len != -1){
        packet_data_len = packet_len;
        saved_bytes = 0;
    }

    if((saved_bytes + len) > WORK_BUFF_SIZE){
        LOG_ERR("Not enougth buffer: %d of %d",(saved_bytes + len),WORK_BUFF_SIZE);
        return -1;
    }

    if((saved_bytes + len) > packet_data_len){
        LOG_ERR("Saved bytes > packet bytes %d of %d",(saved_bytes + len),packet_data_len);
        return -1;
    }

    memcpy(&work_buff[saved_bytes],data,len);
    saved_bytes+=len;

    LOG_DBG("Saved %d of %d",saved_bytes,packet_data_len);

    if(saved_bytes == packet_data_len){
        LOG_DBG("Full packet saved");

        gopro_parse_response_cohn_cert(work_buff,saved_bytes);

        LOG_DBG("Packet parse finish");
    };

    return 0;
};
