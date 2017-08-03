/* ADC1 Example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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
#include "ims_shoesensor.h"
#include "ims_nvs.h"


static const char *TAG = "shoesensor";

globalptrs_t *globalPtrs;

/*
 * Task to calibrate and process sensor measurements.
 * After calibration, measurements are sent to UDP class for transmission
 */
void sensor_eval_task(void *arg)
{
    while(1) {
//        timer_event_t evt;
//        esp_err_t err;
//
//        xQueueReceive(timer_queue, &evt, portMAX_DELAY);
//        if(evt.type == DISABLE_INTERRUPT) {
//
//        	err = timer_pause(TIMER_GROUP_0, TIMER_0);
//        	if(err != ESP_OK){
//        		ESP_LOGI(TAG,"timer_pause error: %d", err);
//        	} else {
//        		ESP_LOGI(TAG,"timer paused");
//        	}
//
//        	err = timer_disable_intr(TIMER_GROUP_0, TIMER_0);
//        	if(err != ESP_OK){
//        		ESP_LOGI(TAG,"timer_disable_intr error: %d", err);
//        	} else {
//        		ESP_LOGI(TAG,"interrupt disabled");
//        	}
//        } else if(evt.type == NODEID_CHANGE) {
//        	xEventGroupClearBits( globalPtrs->system_event_group, NEW_NODEID);
//        	if( !get_flash_uint8( &nodeid, "nodeid") ){
//        		nodeid = (uint8_t) DEFAULT_NODEID;
//        	}
//        	out->nodeid = nodeid;
//        }
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
