#include "canbus_isotp.h"
#include <gopro_client.h>

LOG_MODULE_REGISTER(canbus_isotp, CONFIG_CAN_LOG_LVL);

static void isotp_rx_thread(void *arg1, void *arg2, void *arg3);
static void isotp_tx_thread(void *arg1, void *arg2, void *arg3);

struct k_sem can_isotp_rx_sem;
extern struct k_sem can_reply_sem;

K_THREAD_STACK_DEFINE(isotp_rx_thread_stack, ISOTP_RX_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(isotp_tx_thread_stack, ISOTP_TX_THREAD_STACK_SIZE);

ZBUS_CHAN_DECLARE(can_rx_ble_chan);

ZBUS_CHAN_DEFINE(can_tx_ble_chan,                           	/* Name */
         struct mem_pkt_t,                       		      	/* Message type */
         NULL,                                       	/* Validator */
         NULL,                                       	/* User Data */
         ZBUS_OBSERVERS(can_tx_ble_subscriber),  	        		/* observers */
         ZBUS_MSG_INIT(0)       						/* Initial value */
);

ZBUS_MSG_SUBSCRIBER_DEFINE(can_tx_ble_subscriber);

struct k_thread isotp_rx_thread_data;
struct k_thread isotp_tx_thread_data;

struct isotp_recv_ctx isotp_recv_ctx;

const struct isotp_msg_id isotp_rx_addr = {
	.std_id = 0x753,
};
const struct isotp_msg_id isotp_tx_addr = {
	.std_id = 0x763,
};

const struct isotp_msg_id tx_reply = {
	.std_id = 0x783,
};

const struct isotp_msg_id rx_reply = {
	.std_id = 0x784,
};

const struct isotp_fc_opts isotp_fc_opts = {.bs = 8, .stmin = 10};

void canbus_isotp_init(const struct device *can_dev){
	k_tid_t tid;

    LOG_DBG("Init ISO-TP with can_dev=0x%0X", (uint32_t)can_dev);

    k_sem_init(&can_isotp_rx_sem, 1, 1);

	tid = k_thread_create(&isotp_rx_thread_data, isotp_rx_thread_stack, K_THREAD_STACK_SIZEOF(isotp_rx_thread_stack), isotp_rx_thread, (void *)can_dev, NULL, NULL, ISOTP_RX_THREAD_PRIORITY, 0, K_NO_WAIT);
	if (!tid) {
		LOG_ERR("ERROR spawning ISO-TP rx thread\n");
	}else{
		k_thread_name_set(tid, "isotprx");
	}
	
	tid = k_thread_create(&isotp_tx_thread_data, isotp_tx_thread_stack, K_THREAD_STACK_SIZEOF(isotp_tx_thread_stack), isotp_tx_thread, (void *)can_dev, NULL, NULL, ISOTP_TX_THREAD_PRIORITY, 0, K_NO_WAIT);
	if (!tid) {
		LOG_ERR("ERROR spawning ISO-TP rx thread\n");
	}else{
		k_thread_name_set(tid, "isotptx");
	}

}

static void isotp_rx_thread(void *arg1, void *arg2, void *arg3){
	const struct device *can_dev = arg1;
	int ret, rem_len;
	int err;
	struct net_buf *buf;
	struct mem_pkt_t mem_pkt;

	LOG_DBG("Bind ISO-TP RX, dev 0x%0X",(uint32_t)can_dev);
	ret = isotp_bind(&isotp_recv_ctx, can_dev, &isotp_rx_addr, &isotp_tx_addr, &isotp_fc_opts, K_FOREVER);

	if(ret != ISOTP_N_OK){
		LOG_ERR("Failed to bind ISO-TP to rx ID %d [%d]\n", isotp_rx_addr.std_id, ret);
		return;
	}
	memset(&mem_pkt,0,sizeof(struct mem_pkt_t));

	while (1) {
		if( k_sem_take(&can_isotp_rx_sem, K_MSEC(2000)) == 0) {
			LOG_DBG("Semaphore lock %d",ret);
			if(mem_pkt.data != NULL){
				LOG_DBG("Free isotp mem");
				k_free(mem_pkt.data);
			}
			memset(&mem_pkt,0,sizeof(struct mem_pkt_t));

			do {
				rem_len = isotp_recv_net(&isotp_recv_ctx, &buf, K_FOREVER);

				if(mem_pkt.data == NULL){ //Начало пакета, выделение памяти
					uint32_t alloc_size = rem_len + buf->len;
					LOG_DBG("Start recieving pkt, allocate %d bytes",alloc_size);

					mem_pkt.data = k_malloc(alloc_size);

					if (mem_pkt.data != NULL) {
						memset(mem_pkt.data, 0, alloc_size);
						mem_pkt.len = alloc_size;
					} else {
						LOG_ERR("Memory not allocated");
					}
				}
				
				if (rem_len < 0) {
					LOG_ERR("Receiving error [%d]\n", rem_len);
					mem_pkt.len = -1;
					break;
				}

				if(mem_pkt.data != NULL){
					while (buf != NULL) {
						memcpy(&mem_pkt.data[mem_pkt.index],buf->data,buf->len);
						mem_pkt.index += buf->len;
						LOG_DBG("ISO-TP Proceed %d bytes, %d total",buf->len,mem_pkt.index);
						buf = net_buf_frag_del(NULL, buf);
					}
				}
			} while (rem_len);

			if(mem_pkt.len < 0){
				k_sem_give(&can_isotp_rx_sem);
				break;
			}

			LOG_INF("Got %d bytes of %d in total", mem_pkt.index, mem_pkt.len);

			err = zbus_chan_pub(&can_rx_ble_chan, &mem_pkt, K_NO_WAIT);
			if(err != 0){
				LOG_ERR("Chan pub failed: %d",err);
				k_sem_give(&can_isotp_rx_sem);
				break;
			}
	}else{
		LOG_WRN("Semaphore busy");
	}
	
	}
}

static void isotp_tx_thread(void *arg1, void *arg2, void *arg3){
	const struct device *can_dev = arg1;
	int ret;
	static struct isotp_send_ctx send_ctx;
	struct mem_pkt_t mem_pkt;
	const struct zbus_channel *chan;

	LOG_DBG("ISO-TP TX, dev 0x%0X",(uint32_t)can_dev);

	memset(&send_ctx,0,sizeof(struct isotp_send_ctx));

	while(1){

		while (!zbus_sub_wait_msg(&can_tx_ble_subscriber, &chan, &mem_pkt, K_FOREVER)) {
			if (&can_tx_ble_chan == chan) {
				
				LOG_DBG("Get %d bytes for ISOTP send",mem_pkt.len);

				LOG_HEXDUMP_DBG(mem_pkt.data,mem_pkt.len,"ISOTP_data");

				ret = isotp_send(&send_ctx, can_dev, mem_pkt.data, mem_pkt.len, &tx_reply, &rx_reply, NULL, NULL);
				if (ret != ISOTP_N_OK) {
					LOG_ERR("Error while sending data to ID %d [%d]\n",	tx_reply.std_id, ret);
				}

				k_free(mem_pkt.data);
				k_sem_give(&can_reply_sem);

			}
		}
	}
};