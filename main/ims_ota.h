/*
	OTA for ESP32
	IMS version for XoSoft
	D. Scherly 19.04.2017

	Adpated from ESP32 OTA example
 */

#ifndef __IMS_OTA_H__
#define __IMS_OTA_H__

#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "nvs_flash.h"

#ifdef __cplusplus
extern "C" {
#endif


void ota_start_task (void *pvParameters);
int read_until(char *buffer, char delim, int len);
bool read_past_http_header(char text[], int total_len, esp_ota_handle_t out_handle);
bool connect_to_http_server();
bool ota_init();
bool ota_run(void);

#ifdef __cplusplus
}
#endif


#endif /* __IMS_OTA_H__ */
