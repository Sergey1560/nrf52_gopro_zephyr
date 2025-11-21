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
#include "src/protobuf/gopro_client.pb.h"

#include <pb_encode.h>
#include <pb_decode.h>

#include "gopro_packet.h"
#include "canbus.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gopro_protobuf, CONFIG_PARSE_LOG_LVL);

static char *gopro_pb_provstate(open_gopro_EnumProvisioning state);
static int gopro_pb_req_ap(uint8_t scan_id, uint8_t start_index, uint8_t count);

static char *gopro_pb_result(open_gopro_EnumResultGeneric state);
static char *gopro_pb_state(open_gopro_EnumScanning state);

static char *gopro_pb_cohn_status(open_gopro_EnumCOHNStatus state);
static char *gopro_pb_cohn_state(open_gopro_EnumCOHNNetworkState state);

static void gopro_connect_ap(struct ap_list_t *ap_list, int count);
static uint32_t gopro_prepare_connect_new(uint8_t *data, uint32_t max_len);
static uint32_t gopro_prepare_connect_saved(uint8_t *data, uint32_t max_len);
static uint32_t gopro_prepare_finish_pairing(uint8_t *data, uint32_t max_len);

static void gopro_send_big_data(uint8_t *data, uint32_t len, uint8_t type, uint8_t feature, uint8_t action);

static void can_rx_ble_subscriber_task(void *ptr1, void *ptr2, void *ptr3);

extern struct k_sem can_isotp_rx_sem;
extern struct gopro_state_t gopro_state;

K_SEM_DEFINE(can_reply_sem, 1, 1);

const char pairing_str[]="nrf52";

ZBUS_CHAN_DECLARE(gopro_cmd_chan);
ZBUS_CHAN_DECLARE(can_tx_ble_chan);

ZBUS_CHAN_DEFINE(can_rx_ble_chan,                           	/* Name */
         struct mem_pkt_t,                       		      	/* Message type */
         NULL,                                       	/* Validator */
         NULL,                                       	/* User Data */
         ZBUS_OBSERVERS(can_rx_ble_subscriber),  	        		/* observers */
         ZBUS_MSG_INIT(0)       						/* Initial value */
);


ZBUS_MSG_SUBSCRIBER_DEFINE(can_rx_ble_subscriber);

K_THREAD_DEFINE(can_rx_ble_subscriber_task_id, 2048, can_rx_ble_subscriber_task, NULL, NULL, NULL, 3, 0, 0);

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

uint8_t work_buff[WORK_BUFF_SIZE];
uint8_t ble_data_buff[BLEDATA_BUFF_SIZE];
uint8_t resp_ap_entries_buf[128];

volatile int ap_list_index = 0;
volatile struct ap_list_t ap_list[MAX_AP_LIST_COUNT];

bool pb_encode_bytes(pb_ostream_t *stream, const pb_field_t *field, void * const * arg) {
    struct data_ptr_t *data_encode= (struct data_ptr_t *)*arg;

    if (!pb_encode_tag_for_field(stream, field)){
        LOG_ERR("Encode tag failed\n");
        return false;
    }

    return pb_encode_string(stream, data_encode->data, data_encode->size);
}

bool pb_decode_bytes(pb_istream_t *stream, const pb_field_t *field, void **arg) {
    int bytes = stream->bytes_left;
    struct data_ptr_t *data_decode= (struct data_ptr_t *)*arg;

    if(bytes > data_decode->size){
        LOG_ERR("Read bytes > buff size %d of %d\n",stream->bytes_left,data_decode->size);
        data_decode->size=0;
        return false;
    }

    if (pb_read(stream, (pb_byte_t *)data_decode->data, bytes)) {
        data_decode->size=bytes;
    }else{
        data_decode->size=0;
        return false;
    }

    return true;
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


void gopro_parse_response_cohn_status(uint8_t *data, uint32_t len){
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
            LOG_INF("State: %s",gopro_pb_cohn_state(scan_resp.state));    
        }

        if(scan_resp.has_status){
            LOG_INF("Status: %s",gopro_pb_cohn_status(scan_resp.status));
        }

        if(scan_resp.has_enabled){
            LOG_INF("COHN Enabled: %d",scan_resp.enabled);
        }
    }

};

void gopro_parse_response_cohn_cert(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

    open_gopro_ResponseCOHNCert scan_resp = open_gopro_ResponseCOHNCert_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    struct data_ptr_t decode_data;

    decode_data.data = work_buff;
    decode_data.size = WORK_BUFF_SIZE;
    
    scan_resp.cert.funcs.decode=pb_decode_bytes;
    scan_resp.cert.arg = work_buff;

    err = pb_decode(&stream, open_gopro_ResponseCOHNCert_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(input_data,input_len,"Data:");
    }else{
        if(scan_resp.has_result){
            LOG_DBG("Result: %s, CERT size %d",gopro_pb_result(scan_resp.result),decode_data.size);    
        }
    }
};

void gopro_parse_resp_connect_new(uint8_t *data, uint32_t len){
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

void gopro_parse_resp_connect(uint8_t *data, uint32_t len){
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

void gopro_parse_notif_prov_state(uint8_t *data, uint32_t len){
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

void gopro_parse_request_scan_req(uint8_t *data, uint32_t len){
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

    open_gopro_ResponseGeneric scan_resp = open_gopro_ResponseGeneric_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);

    err = pb_decode(&stream, open_gopro_ResponseGeneric_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(data,len,"Data:");
    }else{
        LOG_DBG("Result: %s",gopro_pb_result(scan_resp.result));
    }
}

void gopro_parse_start_scaning(uint8_t *data, uint32_t len){
    int err;

    open_gopro_NotifStartScanning scan_resp = open_gopro_NotifStartScanning_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);

    err = pb_decode(&stream, open_gopro_NotifStartScanning_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
        LOG_HEXDUMP_DBG(data,len,"Data:");
    }else{
        LOG_INF("Scan_id: %d  Totlal: %d  State: %s",scan_resp.scan_id, scan_resp.total_entries, gopro_pb_state(scan_resp.scanning_state));
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
        return (char *)pb_enum_cohn_status[2];
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

int gopro_finish_pairing(void){
    LOG_DBG("Send RequestPairingFinish cmd");

    uint32_t len=gopro_prepare_finish_pairing(work_buff,WORK_BUFF_SIZE);
    gopro_send_big_data(work_buff,len,GP_CNTRL_HANDLE_NET,0x03,0x01);

    return 0;
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

int gopro_parse_ap_entries(struct gopro_packet_t *gopro_packet){
    int err;

    open_gopro_ResponseGetApEntries resp = open_gopro_ResponseGetApEntries_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(&gopro_packet->data[2], gopro_packet->packet_len);

    resp.entries.funcs.decode = read_ap_entries_callback;
    ap_list_index=0;
    resp.entries.arg = (struct ap_list_t*)ap_list;
    memset((struct ap_list_t *)ap_list,0,sizeof(struct ap_list_t));

    err = pb_decode(&stream, open_gopro_ResponseGetApEntries_fields, &resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
    }else{
        LOG_DBG("AP Decode OK.");
        //gopro_connect_ap((struct ap_list_t *)ap_list, ap_list_index);
    }
    LOG_DBG("AP parse finish");
    return 0;
};

static void __attribute__((unused)) gopro_connect_ap(struct ap_list_t *ap_list, int count){

    for(uint32_t i=0; i<count; i++){

        if( strncmp(ap_list[i].ssid, gopro_state.cohn_net.wifi_ssid,strlen(gopro_state.cohn_net.wifi_ssid)) == 0 ){
            if( (ap_list[i].flags & open_gopro_EnumScanEntryFlags_SCAN_FLAG_ASSOCIATED) > 0){
                LOG_WRN("Already connected to SSID %s", gopro_state.cohn_net.wifi_ssid);
                return;
            }

            if( (ap_list[i].flags & open_gopro_EnumScanEntryFlags_SCAN_FLAG_CONFIGURED) > 0){
                LOG_INF("Connect to saved SSID %s",gopro_state.cohn_net.wifi_ssid);
                int len = gopro_prepare_connect_saved(work_buff,WORK_BUFF_SIZE);
                gopro_send_big_data(work_buff,len,GP_CNTRL_HANDLE_NET,0x02,0x04);

            }else{
                LOG_INF("Connect to new SSID %s",gopro_state.cohn_net.wifi_ssid);
                int len = gopro_prepare_connect_new(work_buff,WORK_BUFF_SIZE);
                gopro_send_big_data(work_buff,len,GP_CNTRL_HANDLE_NET,0x02,0x05);
            }
            
            break;
        }
    }
}

static uint32_t gopro_prepare_connect_new(uint8_t *data, uint32_t max_len){
    int err;
    open_gopro_RequestConnectNew req = open_gopro_RequestConnectNew_init_zero;

    struct data_ptr_t ssid_encode;
    memset(&ssid_encode,0,sizeof(struct data_ptr_t));
    
    ssid_encode.data=(char *)gopro_state.cohn_net.wifi_ssid;
    ssid_encode.size=strlen(gopro_state.cohn_net.wifi_ssid);
    
    req.ssid.arg = &ssid_encode;
    req.ssid.funcs.encode=pb_encode_bytes;

    struct data_ptr_t passw_encode;
    memset(&passw_encode,0,sizeof(struct data_ptr_t));
    
    passw_encode.data=(char *)gopro_state.cohn_net.wifi_pass;
    passw_encode.size=strlen(gopro_state.cohn_net.wifi_pass);
    
    req.password.arg = &passw_encode;
    req.password.funcs.encode=pb_encode_bytes;

    pb_ostream_t stream = pb_ostream_from_buffer(data, max_len);

    err = pb_encode(&stream, open_gopro_RequestConnectNew_fields, &req);

    return stream.bytes_written;

}

static uint32_t gopro_prepare_connect_saved(uint8_t *data, uint32_t max_len){
    int err;
    open_gopro_RequestConnect req = open_gopro_RequestConnect_init_zero;

    struct data_ptr_t ssid_encode;
    memset(&ssid_encode,0,sizeof(struct data_ptr_t));
    
    ssid_encode.data=(char *)gopro_state.cohn_net.wifi_ssid;
    ssid_encode.size=strlen(gopro_state.cohn_net.wifi_ssid);
    
    req.ssid.arg = &ssid_encode;
    req.ssid.funcs.encode=pb_encode_bytes;

    pb_ostream_t stream = pb_ostream_from_buffer(data, max_len);

    err = pb_encode(&stream, open_gopro_RequestConnect_fields, &req);

    return stream.bytes_written;
}

static uint32_t gopro_prepare_finish_pairing(uint8_t *data, uint32_t max_len){
    int err;
    open_gopro_RequestPairingFinish req = open_gopro_RequestPairingFinish_init_zero;

    req.result = open_gopro_EnumPairingFinishState_SUCCESS;

    struct data_ptr_t pairing_encode;
    memset(&pairing_encode,0,sizeof(struct data_ptr_t));
    
    pairing_encode.data=(char *)pairing_str;
    pairing_encode.size=strlen(pairing_str);
    
    req.phoneName.funcs.encode = pb_encode_bytes;
    req.phoneName.arg = &pairing_encode;

    pb_ostream_t stream = pb_ostream_from_buffer(data, max_len);

    err = pb_encode(&stream, open_gopro_RequestPairingFinish_fields, &req);

    return stream.bytes_written;
}

static void gopro_send_buf(uint8_t *data, uint32_t len, uint8_t type){
    struct gopro_cmd_t gopro_cmd; 
    int err;
    uint8_t *data_ptr;

    gopro_cmd.cmd_type = type; //Адрес куда слать

    if(len <= 20){ //5bit packet
        LOG_DBG("5bit packet Len: %d",len);
        gopro_cmd.len = len;

        for(uint32_t i=0; i<len; i++){
            gopro_cmd.data[i] = data[i];
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
        gopro_cmd.data[0] = (((len) >> 8) & 0x1f) | (1 << 5);
        gopro_cmd.data[1] = (uint8_t)((len) & 0xff);
        
        for(uint32_t i=0; i<18; i++){
            gopro_cmd.data[2+i] = data[i];
        }

        //LOG_HEXDUMP_DBG(gopro_cmd.data,gopro_cmd.len,"Packet:");
        err = zbus_chan_pub(&gopro_cmd_chan, &gopro_cmd, K_NO_WAIT);
		if(err != 0){
			if(err == -ENOMSG){
				LOG_ERR("Invalid Gopro state, skip cmd");
			}
			LOG_ERR("CMD chan pub failed: %d",err);
		}

        len = len - 18;
        data_ptr = data+18;
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


static void gopro_send_big_data(uint8_t *data, uint32_t len, uint8_t type, uint8_t feature, uint8_t action){
    int err;
    struct gopro_cmd_t gopro_cmd; 
    uint8_t *data_ptr;

    gopro_cmd.cmd_type = type; //Адрес куда слать
    
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
        
        for(uint32_t i=0; i<16; i++){
            gopro_cmd.data[4+i] = data[i];
        }

        LOG_HEXDUMP_DBG(gopro_cmd.data,gopro_cmd.len,"Packet:");
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

            LOG_HEXDUMP_DBG(gopro_cmd.data,gopro_cmd.len,"Packet:");
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

static uint32_t gopro_decode_wifi_cred(uint8_t *data, uint32_t max_len){
    int err;

    open_gopro_RequestConnectNew req = open_gopro_RequestConnectNew_init_zero;

    pb_istream_t stream = pb_istream_from_buffer(data, max_len);
    
    struct data_ptr_t data_decode_ssid;
    memset(&data_decode_ssid,0,sizeof(struct data_ptr_t));
    
    data_decode_ssid.data=(char *)gopro_state.cohn_net.wifi_ssid;
    data_decode_ssid.size=strlen(gopro_state.cohn_net.wifi_ssid);
    
    req.ssid.funcs.decode = pb_decode_bytes;
    req.ssid.arg = &data_decode_ssid;

    struct data_ptr_t data_decode_pasw;
    memset(&data_decode_pasw,0,sizeof(struct data_ptr_t));
    
    data_decode_pasw.data=(char *)gopro_state.cohn_net.wifi_pass;
    data_decode_pasw.size=strlen(gopro_state.cohn_net.wifi_pass);
    
    req.ssid.funcs.decode = pb_decode_bytes;
    req.ssid.arg = &data_decode_pasw;

    err = pb_decode(&stream, open_gopro_RequestConnectNew_fields, &req);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
    }else{
        LOG_DBG("AP Decode OK.");
    }
    return 0;
}

static void can_rx_ble_subscriber_task(void *ptr1, void *ptr2, void *ptr3){
    struct mem_pkt_t mem_pkt;
    ARG_UNUSED(ptr1);
	ARG_UNUSED(ptr2);
	ARG_UNUSED(ptr3);
	const struct zbus_channel *chan;
    GoproClient_bledata bledata;

	while (!zbus_sub_wait_msg(&can_rx_ble_subscriber, &chan, &mem_pkt, K_FOREVER)) {
		if (&can_rx_ble_chan == chan) {

            LOG_DBG("Unpack msg %d len",mem_pkt.len);
            LOG_HEXDUMP_DBG(mem_pkt.data,mem_pkt.len,"Data:");

            memset(&bledata,0,sizeof(GoproClient_bledata));

            pb_istream_t stream = pb_istream_from_buffer(mem_pkt.data, mem_pkt.len);
            
            struct data_ptr_t data_decode;
            memset(&data_decode,0,sizeof(struct data_ptr_t));

            data_decode.data = ble_data_buff;
            data_decode.size = BLEDATA_BUFF_SIZE;
            bledata.data.funcs.decode=pb_decode_bytes;
            bledata.data.arg=&data_decode;
            
            int err = pb_decode(&stream, GoproClient_bledata_fields, &bledata);

            if(!err){
                LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
            }else{
                LOG_DBG("Data for addr: %d size: %d",bledata.ble_addr, data_decode.size);
                LOG_HEXDUMP_DBG(data_decode.data, data_decode.size,"Decode:");

                if(bledata.ble_addr < GP_CNTRL_HANDLE_END){
                    LOG_DBG("Addr valid");
                    if(gopro_state.state == GP_STATE_CONNECTED){
                        LOG_DBG("State connected, send data"); 
                        gopro_send_buf(data_decode.data, data_decode.size, bledata.ble_addr);
                    }else{
                        LOG_WRN("Not connected, skip sending");
                    }
                }else{
                    if(bledata.ble_addr == BLE_ADDR_SET_WIFI_CRED){
                        LOG_DBG("Parse SET WIFI cmd");
                        gopro_decode_wifi_cred(data_decode.data,data_decode.size);
                    }
                };
            };
            LOG_DBG("Free semaphore");
            k_sem_give(&can_isotp_rx_sem);
        }
	}
};

int can_reply(int32_t ble_addr, uint8_t *data, uint32_t len){
    int err;
    struct mem_pkt_t mem_pkt;
    GoproClient_bledata replyreq;

    LOG_DBG("CAN reply %d bytes",len);

    if(len > WORK_BUFF_SIZE){
        LOG_ERR("Reply too big %d of %d",len,WORK_BUFF_SIZE);
        return -EINVAL;
    }

    if( k_sem_take(&can_reply_sem, K_NO_WAIT) != 0 ){
        LOG_ERR("Sem busy");
        return -EINVAL;
    }

    memset(&mem_pkt,0,sizeof(struct mem_pkt_t));
    mem_pkt.data = k_malloc(WORK_BUFF_SIZE);

    if (mem_pkt.data != NULL) {

        memset(&replyreq,0,sizeof(GoproClient_bledata));

        struct data_ptr_t data_encode;
        memset(&data_encode,0,sizeof(struct data_ptr_t));
        
        data_encode.data = data;
        data_encode.size = len;

        replyreq.ble_addr = ble_addr;
        replyreq.data.funcs.encode = pb_encode_bytes;
        replyreq.data.arg = &data_encode;

        pb_ostream_t stream = pb_ostream_from_buffer(mem_pkt.data, WORK_BUFF_SIZE);
        if(!pb_encode(&stream, GoproClient_bledata_fields, &replyreq)){
            LOG_ERR("Encode failed");
            k_free(mem_pkt.data);
            k_sem_give(&can_reply_sem);
            return -EINVAL;
        }
        mem_pkt.len = stream.bytes_written;

    } else {
        LOG_ERR("Memory not allocated");
        return -ENOMEM;
    }

    err = zbus_chan_pub(&can_tx_ble_chan, &mem_pkt, K_NO_WAIT);
   
    if(err < 0){
        LOG_ERR("Failed pub to chan");
        k_free(mem_pkt.data);
        k_sem_give(&can_reply_sem);
        return -EIO;
    }

    return 0;
}