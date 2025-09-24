#ifndef GOPRO_PROTOBUF_H
#define GOPRO_PROTOBUF_H

#include <zephyr/kernel.h>
#include "gopro_client.h"
#include "gopro_packet.h"

#define WORK_BUFF_SIZE 2048

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
int gopro_build_packet_cohn_status(uint8_t *data, uint32_t len, int32_t packet_len);
int gopro_build_packet_cohn_cert(uint8_t *data, uint32_t len, int32_t packet_len);

void gopro_parse_start_scaning(uint8_t *data, uint32_t len);
int  gopro_parse_ap_entries(struct gopro_packet_t *gopro_packet);
void gopro_parse_response_generic(uint8_t *data, uint32_t len);
void gopro_parse_request_scan_req(uint8_t *data, uint32_t len);
void gopro_parse_resp_connect_new(uint8_t *data, uint32_t len);
void gopro_parse_resp_connect(uint8_t *data, uint32_t len);
void gopro_parse_notif_prov_state(uint8_t *data, uint32_t len);
void gopro_parse_response_cohn_status(uint8_t *data, uint32_t len);
void gopro_parse_response_cohn_cert(uint8_t *data, uint32_t len);

#endif