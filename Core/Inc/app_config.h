#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/*
 * Donanim/P10 varsayilan pin plani (74HCT245 uzerinden):
 *  - STM32 cikislari -> 74HCT245 A tarafina
 *  - 74HCT245 B tarafindan -> P10 HUB12 IDC 2x8 girisine
 *
 * Not: Bu pinler sahada degistirilebilir. Tek yer: bu dosya.
 */

/* HUB12 kontrol hatlari */
#define APP_P10_GPIO_LAT_PORT   GPIOD
#define APP_P10_GPIO_LAT_PIN    GPIO_PIN_12

#define APP_P10_GPIO_OE_PORT    GPIOD
#define APP_P10_GPIO_OE_PIN     GPIO_PIN_13

#define APP_P10_GPIO_A_PORT     GPIOD
#define APP_P10_GPIO_A_PIN      GPIO_PIN_14

#define APP_P10_GPIO_B_PORT     GPIOD
#define APP_P10_GPIO_B_PIN      GPIO_PIN_15

/* HUB12 veri/saat */
#define APP_P10_GPIO_CLK_PORT   GPIOE
#define APP_P10_GPIO_CLK_PIN    GPIO_PIN_2

#define APP_P10_GPIO_DATA_PORT  GPIOE
#define APP_P10_GPIO_DATA_PIN   GPIO_PIN_6

/* Opsiyonel ikinci veri hatti (panel varyantina gore) */
#define APP_P10_HAS_DATA2       0
#define APP_P10_GPIO_DATA2_PORT GPIOE
#define APP_P10_GPIO_DATA2_PIN  GPIO_PIN_5


/* Panel topolojisi */
#define APP_P10_PANEL_W          32
#define APP_P10_PANEL_H          16
#define APP_P10_NUM_PANELS       2
#define APP_P10_MIRROR_PANELS    0  /* 1: ayni goruntu, 0: 2 paneli yan yana (64x16) */

/* Modbus TCP */
#define APP_MODBUS_TCP_PORT      502
#define APP_MODBUS_UNIT_ID       1
#define APP_MODBUS_HR_COUNT      32

/* Holding Register Haritasi */
#define APP_HR_MINUTES           0  /* MMM (0..999) */
#define APP_HR_SECONDS           1  /* SS  (0..59) */
#define APP_HR_YEAR              2  /* YYYY (or: 0 -> kullanma) */
#define APP_HR_MONTH             3  /* 1..12 */
#define APP_HR_DAY               4  /* 1..31 */
#define APP_HR_LOG_ENABLE        5  /* 0/1 */

/* Log ayarlari */
#define APP_LOG_DIR              "logs"
#define APP_LOG_SYNC_PERIOD_MS   2000
#define APP_LOG_SAMPLE_PERIOD_MS 1000

/* Watchdog */
#define APP_WDG_TIMEOUT_MS       6000

#endif /* APP_CONFIG_H */
