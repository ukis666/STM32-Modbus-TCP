#ifndef APP_P10_H
#define APP_P10_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void APP_P10_Init(void);
void APP_P10_SetTime(uint16_t minutes, uint16_t seconds);
void APP_P10_ScanISR(void);
void APP_P10_Task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* APP_P10_H */
