#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <gopro_client.h>
#include <canbus.h>
#include <buttons.h>
#include <leds.h>
#include "gopro_ble_discovery.h"



LOG_MODULE_REGISTER(gopro_main, LOG_LEVEL_DBG);


int main(void)
{

	gopro_gpio_init();
	gopro_leds_init();	
	canbus_init();
	gopro_bt_start();

	return 0;
}
