
#ifndef __IMS_ADC_H__
#define __IMS_ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

void adc1_task(void* arg);
void print_u64(uint64_t val);
void tg0_timer0_init();
void pause_timer0();
void timer_evt_task(void* arg);
void IRAM_ATTR timer_group0_isr(void *para);
void adc_main(void* arg);

adc_data_t *adc_out;


#ifdef __cplusplus
}
#endif

#endif /* __IMS_ADC_H__ */
