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

static int gopro_pb_req_ap(uint8_t scan_id, uint8_t start_index, uint8_t count);

static char *gopro_pb_result(open_gopro_EnumResultGeneric state);
static char *gopro_pb_state(open_gopro_EnumScanning state);

static int gopro_parse_ap_entries(struct ap_entries_desc_t *ap_entries_desc, uint8_t *data, uint32_t len);

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


uint8_t work_buff[WORK_BUFF_SIZE];

struct ap_entries_desc_t ap_entries_desc;

volatile int ap_list_index = 0;
volatile struct ap_list_t ap_list[MAX_AP_LIST_COUNT];

uint8_t resp_ap_entries_buf[128];

size_t gopro_wifi_request_scan(uint8_t *data, uint32_t max_len){
    int err;
    open_gopro_RequestStartScan scan_req = open_gopro_RequestStartScan_init_zero;

    pb_ostream_t stream = pb_ostream_from_buffer(data, max_len);

    err = pb_encode(&stream, open_gopro_RequestStartScan_fields, &scan_req);

    return stream.bytes_written;
}

/*
0a 02 0b 08 05 10 02 18  11 20 00
*/

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
                    ap_entries_desc.packet_data_len = gopro_packet_get_len(gopro_cmd)-2;
                    ap_entries_desc.packet_num = 0;
                    ap_entries_desc.saved_bytes = 0;
                    LOG_DBG("Start AP entires for %d bytes",ap_entries_desc.packet_data_len);
                    gopro_parse_ap_entries(&ap_entries_desc,&gopro_cmd->data[index],len);
                    break;

                default:
                    LOG_WRN("No PARSE for feature 0x02 Action ID 0x%0X",action);
                    break;
            }
        }

    }
    return 0;
}

static void gopro_parse_request_scan_req(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

   	LOG_HEXDUMP_DBG(input_data,input_len,"RESP Decoding data:");

    open_gopro_ResponseStartScanning scan_resp = open_gopro_ResponseStartScanning_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    err = pb_decode(&stream, open_gopro_ResponseStartScanning_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
    }else{
        LOG_DBG("RESP Decode OK.\nResult: %s State: %s",gopro_pb_result(scan_resp.result),gopro_pb_state(scan_resp.scanning_state));
    }
}

static void gopro_parse_start_scaning(uint8_t *data, uint32_t len){
    int err;
    uint8_t *input_data = data;
    uint32_t input_len = len;

   	LOG_HEXDUMP_DBG(input_data,input_len,"Decoding data:");

    open_gopro_NotifStartScanning scan_resp = open_gopro_NotifStartScanning_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(input_data, input_len);

    err = pb_decode(&stream, open_gopro_NotifStartScanning_fields, &scan_resp);

    if(!err){
        LOG_ERR("PB decode failed %s", PB_GET_ERROR(&stream));
    }else{
        LOG_DBG("Notif Decode OK.\nScan_id: %d  Totlal: %d  State: %s",scan_resp.scan_id, scan_resp.total_entries, gopro_pb_state(scan_resp.scanning_state));
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

static int gopro_pb_req_ap(uint8_t scan_id, uint8_t start_index, uint8_t count){
    int err;
    struct gopro_cmd_t gopro_cmd;

    LOG_DBG("Request AP %d",start_index);

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
            for(uint32_t i=0; i<ap_list_index; i++){
                LOG_DBG("SSID: %s Mhz: %d Bars: %d Flags: %d",(char *)ap_list[i].ssid,(int32_t)ap_list[i].signal_frequency_mhz,(int32_t)ap_list[i].signal_strength_bars,(int32_t)ap_list[i].flags);
            }

        }
    }

    LOG_DBG("AP parse finish");
    return 0;
};