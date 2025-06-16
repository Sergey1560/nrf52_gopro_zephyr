#ifndef GOPRO_CANBUS_H
#define GOPRO_CANBUS_H



#define GPCAN_HEART_BEAT_ID   0x734
#define GPCAN_INPUT_MSG_ID    0x740
#define GPCAN_ENABLE_FILTER  

// #define MCP2515_8MHz_500kBPS_CFG1            0x00
// #define MCP2515_8MHz_500kBPS_CFG2            0x90
// #define MCP2515_8MHz_500kBPS_CFG3            0x82

// #define MCP2515_8MHz_500kBPS_CFG1            0x40
// #define MCP2515_8MHz_500kBPS_CFG2            0x91
// #define MCP2515_8MHz_500kBPS_CFG3            0x01

// #define MCP2515_8MHz_500kBPS_CFG1            0xc0
// #define MCP2515_8MHz_500kBPS_CFG2            0x91
// #define MCP2515_8MHz_500kBPS_CFG3            0x01



int canbus_init(void);
int can_hb(void);

#endif