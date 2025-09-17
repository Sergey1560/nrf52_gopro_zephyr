#ifndef GOPRO_PACKET_H
#define GOPRO_PACKET_H

#include <errno.h>
#include <zephyr/kernel.h>

#include "gopro_client.h"

typedef enum _gopro_packet_type_t {
    gopro_packet_5bit= 0,
    gopro_packet_13bit = 1,
    gopro_packet_16bit= 2,
    gopro_packet_cont = 3,
    gopro_packet_invalid = 4
} gopro_packet_type_t;


gopro_packet_type_t gopro_packet_get_type(const struct gopro_cmd_t *gopro_cmd);
int gopro_packet_get_len(const struct gopro_cmd_t *gopro_cmd);
void gopro_packet_get_feature(const struct gopro_cmd_t *gopro_cmd, uint8_t *feature, uint8_t *action);
void gopro_packet_get_data_ptr(const struct gopro_cmd_t *gopro_cmd, uint8_t *index, uint32_t *len);

#endif