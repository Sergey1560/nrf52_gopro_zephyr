#ifndef GOPRO_PROTOBUF_H
#define GOPRO_PROTOBUF_H

#include <zephyr/kernel.h>
#include "gopro_client.h"

struct ap_entries_desc_t{
    uint16_t packet_data_len;
    uint16_t saved_bytes;
    uint8_t packet_num;
};


size_t gopro_wifi_request_scan(uint8_t *data, uint32_t max_len);
int gopro_parse_net_reply(struct gopro_cmd_t *gopro_cmd);
#endif