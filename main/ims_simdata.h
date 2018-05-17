
#ifndef __IMS_SIMDATA_H__
#define __IMS_SIMDATA_H__

#ifdef __cplusplus
extern "C" {
#endif

void tg0_timer0_init();
void pause_timer0();
void timer_evt_task(void* arg);
void IRAM_ATTR timer_group0_isr(void *para);
void simdata_main(void* arg);

sync_data_t *sync_out;
shoe_data_t *shoe_out_r;
shoe_data_t *shoe_out_l;

#ifdef __cplusplus
}
#endif

#endif /* __IMS_SIMDATA_H__ */
