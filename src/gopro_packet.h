#ifndef GOPRO_PACKET_H
#define GOPRO_PACKET_H

#include <errno.h>
#include <zephyr/kernel.h>

#include "gopro_client.h"

struct gopro_packet_t {
    uint32_t  total_len;            //Полная длина данных из всех пакетов, включая поля feature и action
    uint32_t  packet_len;           //Полная полезная длина из всех пакетов (без feature и action)
    uint32_t  pkt_len;              //Длина всех данных в текущем пакете
    uint32_t  data_len;             //Длина данных в текущем пакете
    uint32_t  saved_len;            //Количество сохраненных данных
    uint8_t   pkt_start_index;      //Индекс начала полезной нагрузки в текущем пакете, поле feature
    uint8_t   data_start_index;     //Индекс начала данных в текущем пакете
    uint8_t   packet_type;          //Источник пакета, (cmd, query, settings...)
    uint8_t   feature;              
    uint8_t   action;
    uint8_t   *data;
};

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
void gopro_packet_get_pkt_ptr(const struct gopro_cmd_t *gopro_cmd, uint8_t *index, uint32_t *len);

void gopro_packet_build(struct gopro_cmd_t *gopro_cmd);
void gopro_packet_parse(struct gopro_packet_t *gopro_packet);

#endif