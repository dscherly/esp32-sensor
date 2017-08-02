#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/list.h"
#include "freertos/queue.h"

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_types.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "driver/periph_ctrl.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "driver/uart.h"
#include "tcpip_adapter.h"
#include "lwip/sockets.h"
#include "lwip/ip4_addr.h"
#include "fcntl.h"
#include "soc/timer_group_struct.h"
#include "soc/uart_struct.h"
#include "sdkconfig.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "ims_projdefs.h"
#include "ims_nvs.h"
#include "ims_ota.h"
#include "ims_tcp.h"
#include "ims_udp.h"
#include "ims_adc.h"

static const char *TAG = "main";

static globalptrs_t globalPtrs;

/*Handle AP events*/
esp_err_t event_handler(void *ctx, system_event_t *event) {

	switch (event->event_id) {
	case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits( globalPtrs.wifi_event_group, (CONNECTED_BIT | WIFI_READY));
        ESP_LOGI(TAG, "Wifi ready");
		break;
	case SYSTEM_EVENT_STA_CONNECTED:
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		ESP_LOGI(TAG, "STA disconnected, wifi not ready");
		xEventGroupClearBits( globalPtrs.wifi_event_group, (CONNECTED_BIT | WIFI_READY | UDP_ENABLED));
		break;
	default:
		break;
	}
	return ESP_OK;
}


/*
 * Main function
 */
void app_main(void) {

	nvs_flash_init();

    globalPtrs.wifi_event_group = xEventGroupCreate();
    globalPtrs.system_event_group = xEventGroupCreate();
    globalPtrs.udp_tx_q = xQueueCreate(10, sizeof(adc_data_t));

	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    init_flash_variables(&globalPtrs);
    init_wifi();

	xEventGroupWaitBits( globalPtrs.wifi_event_group, CONNECTED_BIT, false, true, pdMS_TO_TICKS( portMAX_DELAY ) );	//wait for wifi to connect

	xTaskCreate(udp_main_task, "udp_main_task", 8192, (void *) &globalPtrs, 4, NULL);	//start udp task
	xTaskCreate(tcp_task, "tcp_task", 8192, (void *) &globalPtrs, 4, NULL);				//start tcp task
    //xTaskCreate(adc1_task, "adc1_task", 8192, (void *) &globalPtrs, 4, NULL);
	adc_main((void *) &globalPtrs);

	const esp_partition_t *boot_part = esp_ota_get_boot_partition();
	ESP_LOGI(TAG, "boot partition label: %s", boot_part->label)
}


