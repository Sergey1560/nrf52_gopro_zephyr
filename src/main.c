#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <gopro_client.h>
#include <canbus.h>
#include <buttons.h>
#include <leds.h>
#include "gopro_ble_discovery.h"

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

LOG_MODULE_REGISTER(gopro_main, LOG_LEVEL_INF);

// System heap
extern struct sys_heap _system_heap;

#ifdef CONFIG_USE_NRF_SDK
int clocks_start(void)
{
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr) {
		LOG_ERR("Unable to get the Clock manager");
		return -ENXIO;
	}

	sys_notify_init_spinwait(&clk_cli.notify);

	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0) {
		LOG_ERR("Clock request failed: %d", err);
		return err;
	}

	do {
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (!err && res) {
			LOG_ERR("Clock could not be started: %d", res);
			return res;
		}
	} while (err);

	LOG_DBG("HF clock started");
	return 0;
}
#endif

int main(void)
{
	struct sys_memory_stats heap_stats;
	int ret;

	#ifdef CONFIG_USE_NRF_SDK
	clocks_start();
	#endif
	gopro_gpio_init();
	gopro_leds_init();	
	canbus_init();
	gopro_bt_start();

	while(1){
		ret = sys_heap_runtime_stats_get(&_system_heap, &heap_stats);
        if (ret < 0) {
            LOG_ERR("Failed to get heap stats");
        } else {
            LOG_DBG("Heap | Free: %u bytes, Allocated: %u bytes Max: %u bytes", heap_stats.free_bytes, heap_stats.allocated_bytes, heap_stats.max_allocated_bytes);
        }
	
        k_msleep(5000);
	}


	return 0;
}
