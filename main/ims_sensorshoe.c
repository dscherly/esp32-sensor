/* ADC1 Example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "ims_sensorshoe.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "soc/timer_group_struct.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "ims_nvs.h"

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')

static const char *TAG = "shoesensor";

globalptrs_t *globalPtrs;
adc_data_t *in;
udp_sensor_data_t *out;
bool calibrate_running = false;

/*
 * Task to calibrate and process sensor measurements.
 * After calibration, measurements are sent to UDP class for transmission
 */
void sensor_eval_task(void *arg) {

	for(;;){
		if(xQueueReceive( globalPtrs->adc_q, in, pdMS_TO_TICKS(2000))) {
//			ESP_LOGI(TAG,"recv nodeid: %d, counter: %d", in->nodeid, in->counter);
//			ESP_LOGI(TAG,"data:%d,%d,%d,%d thresh:%d,%d,%d,%d", in->data[0],in->data[1],in->data[2],in->data[3],thresh[0],thresh[1],thresh[2],thresh[3]);

			//If raw data mode is set, send raw adc data directly over udp
			if((xEventGroupGetBits(globalPtrs->system_event_group ) & SEND_RAW_DATA_ONLY) > 0) {
				xQueueSend( globalPtrs->udp_tx_q, (void *) in, ( TickType_t ) 0); //dont wait if queue is full
			}

			else if((xEventGroupGetBits(globalPtrs->system_event_group ) & CALIBRATING) > 0) {
				if(!calibrate_running) {
					//reset all calibration arrays
					for (int ii = 0; ii < ADCBUFSIZE; ++ii ){
						max [ii] = 0;
						min [ii] = 0xFFFF;
						thresh[ii] = 0xFFFF;
					}
					calibrate_running = true;
				}

				//calibrate mode running
				for(int ii = 0; ii < ADCBUFSIZE; ++ii) {
					if(in->data[ii] > max[ii])
						max[ii] = in->data[ii];
					else if(in->data[ii] < min[ii])
						min[ii] = in->data[ii];
//					ESP_LOGI(TAG,"data:%d,%d,%d,%d max:%d,%d,%d,%d  min:%d,%d,%d,%d", in.data[0],in.data[1],in.data[2],in.data[3],max[0],max[1],max[2],max[3],min[0],min[1],min[2],min[3]);
				}
			}
			else {
				if(calibrate_running || ((xEventGroupGetBits(globalPtrs->system_event_group) & NEW_THRESHOLD) > 0)) {
					calibrate_running = false;
					xEventGroupClearBits(globalPtrs->system_event_group, NEW_THRESHOLD);
					set_flash_uint8( threshold, "threshold" );
					//ESP_LOGI(TAG,"max:%d,%d,%d,%d  min:%d,%d,%d,%d",max[0],max[1],max[2],max[3],min[0],min[1],min[2],min[3]);
					//calculate threshold
					//ESP_LOGI(TAG, "End calibration or threshold change: threshold = %d", threshold);
					float temp_thresh = (float)threshold / 100;
					for(int ii = 0; ii < ADCBUFSIZE; ++ii) {
						thresh[ii] = (uint16_t)(((float) (max[ii] - min[ii])) * temp_thresh) + min[ii];
					}
					ESP_LOGI(TAG,"thresh:%d,%d,%d,%d",thresh[0],thresh[1],thresh[2],thresh[3]);

					//TODO: save max, min and thresh values to flash
					storeCalibration();
				}
				//calibration not running, apply threshold to sensor values and send to udp task
				out->data = 0;
				for(int jj = 0; jj < ADCBUFSIZE; jj++){
					if(in->data[jj] > thresh[jj]){
						out->data |= ( 1 << jj );	//enable a bit if the sensor measurement is above the threshold
					}
				}
//				ESP_LOGI(TAG,"%c%c%c%c%c%c%c%c", BYTE_TO_BINARY(out->data));
				out->nodeid = in->nodeid;
				out->counter = in->counter;

				xQueueSend( globalPtrs->udp_tx_q, (void *) out, ( TickType_t ) 0); //dont wait if queue is full
			}
		}
	}
}


/*
 * Get measurement arrays from flash so that recalibration isnt necessary each time
 */
void initShoeSensor(void){
	char str[10] = "";
	for(int ii = 0; ii < ADCBUFSIZE; ii++){
		sprintf(str, "max%d", ii);
		if( !get_flash_uint16( &max[ii], str) ){
			max[ii] = 0;
			set_flash_uint16( max[ii], str);
		}
		sprintf(str, "min%d", ii);
		if( !get_flash_uint16( &min[ii], str) ){
			min[ii] = 0xFFFF;
			set_flash_uint16( min[ii], str);
		}
		sprintf(str, "thresh%d", ii);
		if( !get_flash_uint16( &thresh[ii], str) ){
			thresh[ii] = 0xFFFF;
			set_flash_uint16( thresh[ii], str);
		}
	}

	return;
}

/*
 * Store calibration values to flash
 */
void storeCalibration(void){
	char str[10] = "";
	for(int ii = 0; ii < ADCBUFSIZE; ii++){
		sprintf(str, "max%d", ii);
		if( !set_flash_uint16( max[ii], str) ){
			ESP_LOGE(TAG,"Error saving calibration item (max[%d]) to flash",ii);
		}
		vTaskDelay(pdMS_TO_TICKS(5));

		sprintf(str, "min%d", ii);
		if( !set_flash_uint16( min[ii], str) ){
			ESP_LOGE(TAG,"Error saving calibration item (min[%d]) to flash",ii);
		}
		vTaskDelay(pdMS_TO_TICKS(5));

		sprintf(str, "thresh%d", ii);
		if( !set_flash_uint16( thresh[ii], str) ){
			ESP_LOGE(TAG,"Error saving calibration item (thresh[%d]) to flash",ii);
		}
		vTaskDelay(pdMS_TO_TICKS(5));
	}

	return;
}

/**
 * @brief Main function for shoe sensor class
 */
void sensor_main(void* arg)
{
	globalPtrs = (globalptrs_t *) arg;
	out = (udp_sensor_data_t *) malloc (sizeof(udp_sensor_data_t));
	in = (adc_data_t *) malloc (sizeof(adc_data_t));

	//init the measurement arrays
	initShoeSensor();

    xTaskCreate(sensor_eval_task, "sensor_eval_task", 4096, NULL, 5, NULL);
}
