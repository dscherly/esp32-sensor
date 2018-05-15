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
#include "freertos/ringbuf.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "soc/timer_group_struct.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "ims_projdefs.h"
#include "ims_adc.h"
#include "ims_nvs.h"
#include "ims_udp.h"
#include "math.h"

#define TIMER_INTR_SEL TIMER_INTR_LEVEL  /*!< Timer level interrupt */
#define TIMER_GROUP    TIMER_GROUP_0     /*!< Test on timer group 0 */
#define TIMER_DIVIDER   16               /*!< Hardware timer clock divider */
#define TIMER_SCALE    (TIMER_BASE_CLK / TIMER_DIVIDER)  /*!< used to calculate counter value */
#define TIMER_FINE_ADJ   (1.4*(TIMER_BASE_CLK / TIMER_DIVIDER)/1000000) /*!< used to compensate alarm value */
#define TIMER_INTERVAL0_SEC   0.008333333333333333//(0.02)   /*!< test interval for timer 0 */ sample rate of 60Hz
#define TEST_WITHOUT_RELOAD   0   /*!< example of auto-reload mode */
#define TEST_WITH_RELOAD   	1      /*!< example without auto-reload mode */
#define DISABLE_INTERRUPT	2
#define NODEID_CHANGE		3
#define DEBUG				4
#define MERGESORT			5
#define MED_FILT_WINDOW_SIZE	5

static const char *TAG = "adc";

typedef struct {
    int type;                  /*!< event type */
    int group;                 /*!< timer group */
    int idx;                   /*!< timer number */
    uint64_t counter_val;      /*!< timer counter value */
    double time_sec;           /*!< calculated time from counter value */
} timer_event_t;


globalptrs_t *globalPtrs;

xQueueHandle timer_queue;

/*
 * @brief timer group0 hardware timer0 init
 */
void tg0_timer0_init()
{
    int timer_group = TIMER_GROUP_0;
    int timer_idx = TIMER_0;
    timer_config_t config;
    config.alarm_en = 1;
    config.auto_reload = 0;
    config.counter_dir = TIMER_COUNT_UP;
    config.divider = TIMER_DIVIDER;
    config.intr_type = TIMER_INTR_SEL;
    config.counter_en = TIMER_PAUSE;
    /*Configure timer*/
    timer_init(timer_group, timer_idx, &config);
    /*Stop timer counter*/
    timer_pause(timer_group, timer_idx);
    /*Load counter value */
    timer_set_counter_value(timer_group, timer_idx, 0x00000000ULL);
    /*Set alarm value*/
    timer_set_alarm_value(timer_group, timer_idx, TIMER_INTERVAL0_SEC * TIMER_SCALE - TIMER_FINE_ADJ);
    /*Enable timer interrupt*/
    timer_enable_intr(timer_group, timer_idx);
    /*Set ISR handler*/
    timer_isr_register(timer_group, timer_idx, timer_group0_isr, (void*) timer_idx, ESP_INTR_FLAG_IRAM, NULL);
    /*Start timer counter*/
    timer_start(timer_group, timer_idx);
}

void timer_evt_task(void *arg)
{
    while(1) {
        timer_event_t evt;
        esp_err_t err;

        xQueueReceive(timer_queue, &evt, portMAX_DELAY);
        if(evt.type == DISABLE_INTERRUPT) {

        	err = timer_pause(TIMER_GROUP_0, TIMER_0);
        	if(err != ESP_OK){
        		ESP_LOGI(TAG,"timer_pause error: %d", err);
        	} else {
        		ESP_LOGI(TAG,"timer paused");
        	}

        	err = timer_disable_intr(TIMER_GROUP_0, TIMER_0);
        	if(err != ESP_OK){
        		ESP_LOGI(TAG,"timer_disable_intr error: %d", err);
        	} else {
        		ESP_LOGI(TAG,"interrupt disabled");
        	}
        } else if(evt.type == NODEID_CHANGE) {
        	xEventGroupClearBits( globalPtrs->system_event_group, NEW_NODEID);
        	if( !get_flash_uint8( &(shoe_out->msgid), "nodeid") ){
        		shoe_out->msgid = (uint8_t) DEFAULT_NODEID;
        	}
        } else if (evt.type == DEBUG) {
        	xEventGroupClearBits( globalPtrs->system_event_group, DEBUG);
//        	ESP_LOGI(TAG,"nodeid = %d, counter = %d", shoe_out->nodeid, shoe_out->counter);
//        	ESP_LOGI(TAG,"adc0_filter: %d %d %d", adc0_filter[0], adc0_filter[1], adc0_filter[2]);
//        	ESP_LOGI(TAG,"b: %d %d %d", b[0], b[1], b[2]);
        }
    }
}


/*
 * @brief timer group0 ISR handler
 */
void IRAM_ATTR timer_group0_isr(void *para)
{
    int timer_idx = (int) para;
    timer_event_t evt;
    uint32_t intr_status = TIMERG0.int_st_timers.val;

    if((intr_status & BIT(timer_idx)) && timer_idx == TIMER_0) {
        TIMERG0.hw_timer[timer_idx].update = 1;
        TIMERG0.int_clr_timers.t0 = 1;
        uint64_t timer_val = ((uint64_t) TIMERG0.hw_timer[timer_idx].cnt_high) << 32 | TIMERG0.hw_timer[timer_idx].cnt_low;

        if( (xEventGroupGetBitsFromISR( globalPtrs->system_event_group ) & FW_UPDATING) > 0 ){
        	evt.type = DISABLE_INTERRUPT;
        	xQueueSendFromISR(timer_queue, &evt, NULL);
        }

        if( (xEventGroupGetBitsFromISR( globalPtrs->system_event_group ) & NEW_NODEID) > 0 ){
        	evt.type = NODEID_CHANGE;
        	xQueueSendFromISR(timer_queue, &evt, NULL);
        }

        //send sync packet first
        sync_out->timestamp++;
        sync_out->sync = (uint16_t) 1;
        sync_out->crc = getCRC8( (uint8_t*) sync_out, sizeof(sync_data_t) ); //TODO check this
        xQueueSendFromISR( globalPtrs->sync_tx_q, (void *) sync_out, ( TickType_t ) 0);

        //send simulated insole data
        shoe_out->timestamp++;
        shoe_out->data[0] = ((uint16_t) rand()) >> 7;
        shoe_out->data[1] = (((uint16_t) rand()) >> 7) + 1000;
        shoe_out->data[2] = (((uint16_t) rand()) >> 7) + 2000;
        shoe_out->data[3] = (((uint16_t) rand()) >> 7) + 3000;
        shoe_out->crc = getCRC8( (uint8_t*) shoe_out, sizeof(shoe_data_t) ); //TODO check this
		xQueueSendFromISR( globalPtrs->shoe_tx_q, (void *) shoe_out, ( TickType_t ) 0);

        /*For a timer that will not reload, we need to set the next alarm value each time. */
        timer_val += (uint64_t) (TIMER_INTERVAL0_SEC * (TIMER_BASE_CLK / TIMERG0.hw_timer[timer_idx].config.divider));

        /*Fine adjust*/
        timer_val -= TIMER_FINE_ADJ;

        TIMERG0.hw_timer[timer_idx].alarm_high = (uint32_t) (timer_val >> 32);
        TIMERG0.hw_timer[timer_idx].alarm_low = (uint32_t) timer_val;

        /*After set alarm, we set alarm_en bit if we want to enable alarm again.*/
        TIMERG0.hw_timer[timer_idx].config.alarm_en = 1;
    }
}

/**
 * @brief In this test, we will test hardware timer0 and timer1 of timer group0.
 */
void adc_main(void* arg)
{
	globalPtrs = (globalptrs_t *) arg;

	sync_out = (sync_data_t *) malloc (sizeof(sync_data_t));
	shoe_out = (shoe_data_t *) malloc (sizeof(shoe_data_t));

	sync_out->startbyte = 165;
	sync_out->len = 4;
	sync_out->msgid = 40;
	sync_out->timestamp = 0;
	sync_out->sync = 0;
	sync_out->crc = 0;
    ESP_LOGI(TAG,"sizeof(sync_data_t): %d",sizeof(sync_data_t))

	shoe_out->startbyte = 165;
	shoe_out->len = 10;
	shoe_out->msgid = 5;
	shoe_out->timestamp = 0;
	shoe_out->crc = 0;
    ESP_LOGI(TAG,"sizeof(adc_data_t): %d",sizeof(shoe_data_t))


	timer_queue = xQueueCreate(10, sizeof(timer_event_t));
	tg0_timer0_init();
    xTaskCreate(timer_evt_task, "timer_evt_task", 2048, NULL, 5, NULL);
}
