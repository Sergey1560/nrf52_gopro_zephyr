#ifndef GOPRO_PROTOBUF_H
#define GOPRO_PROTOBUF_H

#include <zephyr/kernel.h>
#include "gopro_client.h"

#define WORK_BUFF_SIZE 8192

struct ap_entries_desc_t{
    uint16_t packet_data_len;
    uint16_t saved_bytes;
    uint8_t packet_num;
};

#define MAX_SSID_LEN        40
#define MAX_AP_LIST_COUNT   30

struct ap_list_t{
    int32_t signal_frequency_mhz;
    int32_t signal_strength_bars;
    int32_t flags;
    char ssid[MAX_SSID_LEN];
};



size_t gopro_wifi_request_scan(uint8_t *data, uint32_t max_len);
int gopro_parse_net_reply(struct gopro_cmd_t *gopro_cmd);
#endif