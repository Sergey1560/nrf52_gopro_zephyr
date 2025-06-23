#ifndef GOPRO_IDS_H
#define GOPRO_IDS_H



/*
https://gopro.github.io/OpenGoPro/ble/features/query.html#
*/
#define GOPRO_QUERY_STATUS_GET_DATE             0x0E
#define GOPRO_QUERY_STATUS_GET_HW_INFO          0x3C
#define GOPRO_QUERY_STATUS_GET_LOCAL_DATE       0x10
#define GOPRO_QUERY_STATUS_GET_LAST_MEDIA       0xF5
#define GOPRO_QUERY_STATUS_GET_VERSION          0x51
#define GOPRO_QUERY_STATUS_GET_SETTING          0x12
#define GOPRO_QUERY_STATUS_GET_STATUS           0x13
#define GOPRO_QUERY_STATUS_GET_SETTING_CAP      0x32
#define GOPRO_QUERY_STATUS_REG_SETTING          0x52
#define GOPRO_QUERY_STATUS_REG_STATUS           0x53
#define GOPRO_QUERY_STATUS_REG_SETTING_CAP      0x62
#define GOPRO_QUERY_STATUS_UNREG_SETTING        0x72
#define GOPRO_QUERY_STATUS_UNREG_STATUS         0x73
#define GOPRO_QUERY_STATUS_UNREG_SETTING_CAP    0x82


/*
https://gopro.github.io/OpenGoPro/ble/features/statuses.html#status-ids
*/
#define GOPRO_STATUS_ID_BAT_PRESENT             1
#define GOPRO_STATUS_ID_BAT_BARS                2
#define GOPRO_STATUS_ID_OVERHEATING             6
#define GOPRO_STATUS_ID_BUSY                    8
#define GOPRO_STATUS_ID_QUICK_CAPTURE           9
#define GOPRO_STATUS_ID_ENCODING                10
#define GOPRO_STATUS_ID_LCD_LOCK                11
#define GOPRO_STATUS_ID_ENCODING_DURATION       13
#define GOPRO_STATUS_ID_WIRELESS_EN             17
#define GOPRO_STATUS_ID_PAIRING_STATE           19
#define GOPRO_STATUS_ID_LAST_PAIRING_TYPE       20
#define GOPRO_STATUS_ID_LAST_PAIRING_SUCS       21
#define GOPRO_STATUS_ID_REMAIN_VIDEO_TIME       35
#define GOPRO_STATUS_ID_VIDEO_NUM               39
#define GOPRO_STATUS_ID_POLL_PERIOD             60
#define GOPRO_STATUS_ID_GPS_LOCK                68
#define GOPRO_STATUS_ID_AP_MODE                 69
#define GOPRO_STATUS_ID_BAT_PERCENT             70
#define GOPRO_STATUS_ID_MIC_ACC                 74
#define GOPRO_STATUS_ID_READY                   82
#define GOPRO_STATUS_ID_SD_ERRORS               112


#endif