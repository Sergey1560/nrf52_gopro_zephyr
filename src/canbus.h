#ifndef GOPRO_CANBUS_H
#define GOPRO_CANBUS_H

#define GPCAN_HEART_BEAT_ID         0x734

#define GPCAN_INPUT_CMD_ID          0x772
#define GPCAN_REPLY_MSG_CMD_ID      0x773

#define GPCAN_INPUT_SET_ID          0x774
#define GPCAN_REPLY_MSG_SETTINGS_ID 0x775

#define GPCAN_INPUT_QUERY_ID        0x776
#define GPCAN_REPLY_MSG_QUERY_ID    0x777

#define GPCAN_INPUT_NET_ID          0x778
#define GPCAN_REPLY_MSG_NET_ID      0x779

#define GPCAN_INPUT_CONTROL_ID      0x77A

#define GPCAN_REPLY_MSG_ERR_ID      0x740

#define GPCAN_ENABLE_FILTER  


int canbus_init(void);
int can_hb(void);

#endif