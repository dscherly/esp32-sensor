
#ifndef __IMS_SENSORSHOE_H__
#define __IMS_SENSORSHOE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "ims_projdefs.h"

void sensor_eval_task(void* arg);
void sensor_main(void* arg);
void initShoeSensor(void);
void storeCalibration(void);


uint16_t max[ADCBUFSIZE];
uint16_t min[ADCBUFSIZE];
uint16_t thresh[ADCBUFSIZE];

#ifdef __cplusplus
}
#endif

#endif /* __IMS_SENSORSHOE_H__ */
