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
#include "ims_projdefs.h"
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
udp_sensor_data_t out;
uint16_t max[ADCBUFSIZE];
uint16_t min[ADCBUFSIZE];
uint16_t thresh[ADCBUFSIZE];
bool calibrate_running = false;


/*
 * Task to calibrate and process sensor measurements.
 * After calibration, measurements are sent to UDP class for transmission
 */
void sensor_eval_task(void *arg) {
	adc_data_t in;
	char tmp[10];

	for(;;){
		if(xQueueReceive( globalPtrs->adc_q, &in, pdMS_TO_TICKS(2000))) {
			ESP_LOGI(TAG,"data:%d,%d,%d,%d", in.data[0],in.data[1],in.data[2],in.data[3]);
			if((xEventGroupGetBits(globalPtrs->system_event_group ) & CALIBRATE_START)) {
				if(!calibrate_running) {
					memset( max, 0x00, sizeof(uint16_t)*ADCBUFSIZE );
					for (int ii = 0; ii < ADCBUFSIZE; ++ii ){
						min [ii] = 65535;
					}
					memset( thresh, 0x00, sizeof(uint16_t)*ADCBUFSIZE );
					calibrate_running = true;
				}

				//calibrate mode running
				for(int ii = 0; ii < ADCBUFSIZE; ++ii) {
					if(in.data[ii] > max[ii])
						max[ii] = in.data[ii];
					else if(in.data[ii] < min[ii])
						min[ii] = in.data[ii];
//					ESP_LOGI(TAG,"data:%d,%d,%d,%d max:%d,%d,%d,%d  min:%d,%d,%d,%d", in.data[0],in.data[1],in.data[2],in.data[3],max[0],max[1],max[2],max[3],min[0],min[1],min[2],min[3]);
				}
				//TODO send raw data?
			}
			else if((xEventGroupGetBits(globalPtrs->system_event_group ) & CALIBRATE_STOP)) {
				if(calibrate_running) {
					calibrate_running = false;
					ESP_LOGI(TAG,"max:%d,%d,%d,%d  min:%d,%d,%d,%d",max[0],max[1],max[2],max[3],min[0],min[1],min[2],min[3]);
					//calculate threshold
					for(int ii = 0; ii < ADCBUFSIZE; ++ii) {
						thresh[ii] = (uint16_t)(((float) (max[ii] - min[ii])) * ((float)threshold / 100) ) + min[ii];
					}
					ESP_LOGI(TAG,"thresh:%d,%d,%d,%d",thresh[0],thresh[1],thresh[2],thresh[3]);
				} //else {
					//calibration not running, apply threshold to sensor values and send to udp task
//					for(int aa = 0; aa < ADCBUFSIZE; aa++) {
//						ESP_LOGI(TAG,"in.data[%d]=%d, thresh[%d]=%d",aa,in.data[aa],aa,thresh[aa]);
////						if( in.data[aa] > thresh[aa] ){
////							//						out.data = ( 1 << ii ) | out.data;	//enable bit in data byte
////							//						tmp[ii] = '1';
////						}
////						else {
////							//						out.data = ~(1 << ii) & (out.data) ;	//boolean operation to disable bit in data byte
////							//						tmp[ii] = '0';
////						}
//					}
					//				ESP_LOGI(TAG,"tmp: %s", tmp);
					//				ESP_LOGI(TAG,"sensor data byte: %c%c%c%c%c%c%c%c", BYTE_TO_BINARY(out.data));

				//}
			}
		}
	}
}

/**
 * @brief Main function for shoe sensor class
 */
void sensor_main(void* arg)
{
	globalPtrs = (globalptrs_t *) arg;

    xTaskCreate(sensor_eval_task, "sensor_eval_task", 4096, NULL, 5, NULL);
}
