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
#include "ims_adc.h"
#include "ims_nvs.h"


#define ADC1_CH4 (4)
#define ADC1_CH5 (5)
#define ADC1_CH6 (6)
#define ADC1_CH7 (7)

#define TIMER_INTR_SEL TIMER_INTR_LEVEL  /*!< Timer level interrupt */
#define TIMER_GROUP    TIMER_GROUP_0     /*!< Test on timer group 0 */
#define TIMER_DIVIDER   16               /*!< Hardware timer clock divider */
#define TIMER_SCALE    (TIMER_BASE_CLK / TIMER_DIVIDER)  /*!< used to calculate counter value */
#define TIMER_FINE_ADJ   (1.4*(TIMER_BASE_CLK / TIMER_DIVIDER)/1000000) /*!< used to compensate alarm value */
#define TIMER_INTERVAL0_SEC   0.0166666666666666667//(0.02)   /*!< test interval for timer 0 */ sample rate of 60Hz
#define TEST_WITHOUT_RELOAD   0   /*!< example of auto-reload mode */
#define TEST_WITH_RELOAD   1      /*!< example without auto-reload mode */
#define DISABLE_INTERRUPT	2

static const char *TAG = "adc";

typedef struct {
    int type;                  /*!< event type */
    int group;                 /*!< timer group */
    int idx;                   /*!< timer number */
    uint64_t counter_val;      /*!< timer counter value */
    double time_sec;           /*!< calculated time from counter value */
} timer_event_t;


globalptrs_t *globalPtrs;
adc_data_t *out;
uint8_t nodeid;

xQueueHandle timer_queue;

/*
 * @brief Print a uint64_t value
 */
void print_u64(uint64_t val)
{
    printf("0x%08x%08x\n", (uint32_t) (val >> 32), (uint32_t) (val));
}

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
        /*Timer0 is an example that doesn't reload counter value*/
        TIMERG0.hw_timer[timer_idx].update = 1;
        TIMERG0.int_clr_timers.t0 = 1;
        uint64_t timer_val = ((uint64_t) TIMERG0.hw_timer[timer_idx].cnt_high) << 32 | TIMERG0.hw_timer[timer_idx].cnt_low;

        if( (xEventGroupGetBits( globalPtrs->wifi_event_group ) & PAUSE_ADC) > 0 ){
        	xEventGroupClearBits( globalPtrs->wifi_event_group, PAUSE_ADC);
        	evt.type = DISABLE_INTERRUPT;
        	xQueueSendFromISR(timer_queue, &evt, NULL);
        }

        //check if nodeid has been changed
        if( (xEventGroupGetBits( globalPtrs->wifi_event_group ) & NEW_NODEID) > 0 ){
        	xEventGroupClearBits( globalPtrs->wifi_event_group, NEW_NODEID);
        	if( !get_flash_uint8( &nodeid, "nodeid") ){
        		nodeid = (uint8_t) DEFAULT_NODEID;
        	}
        	out->nodeid = nodeid;
        }

        //read adc data
        out->data[0] =  (uint16_t) adc1_get_voltage(ADC1_CH4);
        out->data[1] =  (uint16_t) adc1_get_voltage(ADC1_CH5);
        out->data[2] =  (uint16_t) adc1_get_voltage(ADC1_CH6);
        out->data[3] =  (uint16_t) adc1_get_voltage(ADC1_CH7);

        xQueueSendFromISR( globalPtrs->udp_tx_q, (void *) out, ( TickType_t ) 0); //dont wait if queue is full
        out->counter++;

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

	// initialize ADC
	adc1_config_width(ADC_WIDTH_12Bit);
	adc1_config_channel_atten(ADC1_CH4,ADC_ATTEN_11db);
	adc1_config_channel_atten(ADC1_CH5,ADC_ATTEN_11db);
	adc1_config_channel_atten(ADC1_CH6,ADC_ATTEN_11db);
	adc1_config_channel_atten(ADC1_CH7,ADC_ATTEN_11db);

	if( !get_flash_uint8( &nodeid, "nodeid") ){
		nodeid = (uint8_t) DEFAULT_NODEID;
	}

	out = (adc_data_t *) malloc (sizeof(adc_data_t));

	out->nodeid = nodeid;
	out->size = sizeof(out->data);
	out->counter = 0;

	timer_queue = xQueueCreate(10, sizeof(timer_event_t));
	tg0_timer0_init();
    xTaskCreate(timer_evt_task, "timer_evt_task", 2048, NULL, 5, NULL);
}
