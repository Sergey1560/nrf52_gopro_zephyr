#include "gopro_protobuf.h"

#include <errno.h>
#include <zephyr/kernel.h>

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


#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gopro_protobuf, LOG_LEVEL_DBG);


size_t gopro_wifi_request_scan(uint8_t *data, uint32_t max_len){
    int err;
    open_gopro_RequestStartScan scan_req = open_gopro_RequestStartScan_init_zero;

    pb_ostream_t stream = pb_ostream_from_buffer(data, max_len);

    err = pb_encode(&stream, open_gopro_RequestStartScan_fields, &scan_req);

    return stream.bytes_written;
}