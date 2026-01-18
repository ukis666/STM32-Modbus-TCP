#ifndef APP_P10_H
#define APP_P10_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void APP_P10_Init(void);

/**
 * Timer interrupt icinde cagrilacak tarama ISR fonksiyonu.
 * (HAL_TIM_PeriodElapsedCallback icinde TIM7 icin)
 */
void APP_P10_ScanISR(void);

/**
 * minutes: 0..999 (MMM)
 * seconds: 0..59  (SS, sag panelde (bos,SS) olarak saga hizali)
 */
void APP_P10_SetTime(uint16_t minutes, uint16_t seconds);

/**
 * FreeRTOS gorevi:
 * - TIM7 scan timer'i baslatir
 * - Supervisor kick atar
 */
void APP_P10_Task(void *argument);

#ifdef __cplusplus
}
#endif

#endif // APP_P10_H
