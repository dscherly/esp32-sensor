/*
 * UART functions for ESP32
 * XoSoft Project
 * D. Scherly 20.04.2017
*/
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "ims_nvs";

/*
 * Set variables in non-volatile memory
 */
bool erase_flash_key( const char *label ){
	nvs_handle my_handle;
	esp_err_t err = nvs_open(label, NVS_READWRITE, &my_handle);

	if (err == ESP_OK) {
		err = nvs_erase_key(my_handle, label);

		switch (err) {
		case ESP_OK:
			nvs_commit(my_handle);
			return true;
		case ESP_ERR_NVS_NOT_FOUND:
			return true;
		default:
			ESP_LOGE(TAG,"Error (%d) Could not erase from flash", err);
			break;
		}
	}
	return false;
}

/*
 * Set variables in non-volatile memory
 */
bool set_flash_uint32( uint32_t ip, const char *label ){
	nvs_handle my_handle;
	esp_err_t err = nvs_open(label, NVS_READWRITE, &my_handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG,"Error (%d) opening NVS handle!", err);
	} else {
		//write
		//ESP_LOGI(TAG,"Writing to flash: %s", label);
		err = nvs_set_u32(my_handle, label, ip);

		switch (err) {
		case ESP_OK:
			return true;
		default :
			ESP_LOGE(TAG,"Error (%d) writing to flash", err);
			break;
		}
	}
	return false;
}

/*
 * get an ip address from flash
 */
bool get_flash_uint32( uint32_t *ip, const char *label ){
	nvs_handle my_handle;
	esp_err_t err = nvs_open(label, NVS_READWRITE, &my_handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG,"Error (%d) opening NVS handle!", err);
	} else {
		err = nvs_get_u32(my_handle, label, ip);

		switch (err) {
		case ESP_OK:
			return true;
		case ESP_ERR_NVS_NOT_FOUND:
//			ESP_LOGI(TAG,"The value \"%s\" is not initialized yet", label);
			break;
		default :
			ESP_LOGE(TAG,"Error (%d) reading\n", err);
			break;
		}
	}
	return false;
}

/*
 * get a string from flash
 */
bool get_flash_str( char *str, const char *label){
	nvs_handle my_handle;
	size_t required_size;

	esp_err_t err = nvs_open(label, NVS_READWRITE, &my_handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG,"Error (%d) opening NVS handle!", err);
	} else {
		//if the string is present, get its length
		err = nvs_get_str(my_handle, label, 0, &required_size);
		//ESP_LOGI(TAG,"nvs_get_str1 returned code: %d, required size: %d", err, required_size);
		switch (err) {
		case ESP_OK:
			break;
		case ESP_ERR_NVS_INVALID_LENGTH:
			ESP_LOGE(TAG,"(%d) Invalid string length", err);
			return false;
		default :
			ESP_LOGE(TAG,"(%d) Error", err);
			return false;
		}

		//if the string is present, get it
		err = nvs_get_str(my_handle, label, str, &required_size);
		switch (err) {
		case ESP_OK:
			//ESP_LOGI(TAG,"string \"%s\" found in flash", str);
			return true;
		case ESP_ERR_NVS_NOT_FOUND:
//			ESP_LOGE(TAG,"\"%s\" is not initialized yet", label);
			break;
		default :
			ESP_LOGE(TAG,"(%d) Error", err);
			break;
		}
	}
	return false;
}

/*
 * Set variables in non-volatile memory
 */
bool set_flash_str( char *str, const char *label ){
	nvs_handle my_handle;
	esp_err_t err = nvs_open(label, NVS_READWRITE, &my_handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG,"Error (%d) opening NVS handle!", err);
	} else {
		//write
		err = nvs_set_str(my_handle, label, str);

		switch (err) {
		case ESP_OK:
			//ESP_LOGI(TAG,"string \"%s\" written to flash", str);
			return true;
		default :
			ESP_LOGE(TAG,"Error (%d) writing to flash", err);
			break;
		}
	}
	return false;
}

/*
 * get an 8bit value address from flash
 */
bool get_flash_uint8( uint8_t *value, const char *label ){
	nvs_handle my_handle;
	esp_err_t err = nvs_open(label, NVS_READWRITE, &my_handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG,"Error (%d) opening NVS handle!", err);
	} else {
		err = nvs_get_u8(my_handle, label, value);

		switch (err) {
		case ESP_OK:
			return true;
		case ESP_ERR_NVS_NOT_FOUND:
//			ESP_LOGI(TAG,"The value \"%s\" is not initialized yet", label);
			break;
		default :
			ESP_LOGE(TAG,"Error (%d) reading\n", err);
			break;
		}
	}
	return false;
}

/*
 * Set variables in non-volatile memory
 */
bool set_flash_uint8( uint8_t value, const char *label ){
	nvs_handle my_handle;
	esp_err_t err = nvs_open(label, NVS_READWRITE, &my_handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG,"Error (%d) opening NVS handle!", err);
	} else {
		//write
		//ESP_LOGI(TAG,"Writing to flash: %s", label);
		err = nvs_set_u8(my_handle, label, value);

		switch (err) {
		case ESP_OK:
			return true;
		default :
			ESP_LOGE(TAG,"Error (%d) writing to flash", err);
			break;
		}
	}
	return false;
}
