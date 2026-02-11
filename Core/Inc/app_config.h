#pragma once

/*
 * app_config.h
 *
 * Tek noktadan konfig.
 * - Modbus HR map (32 HR)
 * - Watchdog / log ayarlari
 * - P10 HUB12 pin ve tarama ayarlari
 *
 * Not: Bu projede CubeMX main.h icinde pin macro uretmiyor.
 * Bu nedenle P10 pinleri burada dogrudan tanimli.
 * Sahada kendi kablolamana gore sadece burayi degistirmen yeterli.
 */

#include "stm32f4xx_hal.h"

// ============================================================
// MODBUS HOLDING REGISTERS
// ============================================================

#define APP_MODBUS_HR_COUNT 32u

/* HR address map (Holding Registers)
 *  HR0  : MMM (minutes)
 *  HR1  : SS  (seconds)
 *  HR2  : YYYY
 *  HR3  : MM
 *  HR4  : DD
 *  HR5  : LOG_ENABLE (0/1)
 *  HR6..HR31: reserved / payload
 */
#define APP_HR_MINUTES    0u
#define APP_HR_SECONDS    1u
#define APP_HR_YEAR       2u
#define APP_HR_MONTH      3u
#define APP_HR_DAY        4u
#define APP_HR_LOG_ENABLE 5u

// ============================================================
// WATCHDOG
// ============================================================

/* IWDG timeout (ms). 8s: sahada toleransli ve stabil. */
#define APP_WDG_TIMEOUT_MS 8000u

// ============================================================
// LOGGING
// ============================================================

/* SD card directory */
#define APP_LOG_DIR "logs"

/* Queue wait / task pacing */
#define APP_LOG_SAMPLE_PERIOD_MS 250u

/* fsync period */
#define APP_LOG_SYNC_PERIOD_MS 1000u

/* Payload block to append into CSV (HR6..HR15) */
#define APP_LOG_PAYLOAD_START_HR 6u
#define APP_LOG_PAYLOAD_COUNT_HR 10u

// ============================================================
// P10 HUB12
// ============================================================

/* 2 panel zincir (panel-1 OUT -> panel-2 IN) */
#define APP_P10_CHAIN 2

/* OE aktif seviye (cogu panelde active-low) */
#define APP_P10_OE_ACTIVE_LOW 1

/* Panelde C adres hatti varsa 1 yap (A/B/C) */
//#define APP_P10_HAS_C 0   // ESKI
#define APP_P10_HAS_C 1     // YENI: C hattini kullan (1/8 scan)

/* Bit kaydirma yonu. Ters cikarsa 0 yap. */
#define APP_P10_SHIFT_MSB_FIRST 1

/* Paneller swap gorunuyorsa 1 yap */
#define APP_P10_SWAP_PANELS 0

/* Her panel X aynalama gerekiyorsa 1 yap */
#define APP_P10_MIRROR_EACH_PANEL_X 0

/* P10 scan timer: TIM7 (TIM6 HAL tick icin kullaniliyor) */
#define APP_P10_TIM_INSTANCE TIM7

/* Tarama ISR frekansi (Hz). 4kHz -> flicker yok, CPU kabul edilebilir. */
#define APP_P10_SCAN_IRQ_HZ 4000u

// ---------------- P10 PIN MAP (Black Board P4/P5 Header) ----------------
// Bu kartta P10 hatlari P4/P5 header uzerinden Port A/B'ye alindi.
// Baglanti yaparken pin numarasi saymak yerine kart ustu etiketleri (A6/A4/A5/A3/A0, B1/B0) ile git.

// DATA1 (R1 / ust grup)
#define APP_P10_DATA1_GPIO_Port GPIOA
#define APP_P10_DATA1_Pin       GPIO_PIN_6   // P4: A6

// DATA2 (R2 / alt grup)
#define APP_P10_DATA2_GPIO_Port GPIOA
#define APP_P10_DATA2_Pin       GPIO_PIN_4   // P4: A4

// CLK
#define APP_P10_CLK_GPIO_Port   GPIOA
#define APP_P10_CLK_Pin         GPIO_PIN_5   // P4: A5

// LAT / STB
#define APP_P10_LAT_GPIO_Port   GPIOB
#define APP_P10_LAT_Pin         GPIO_PIN_0   // P5: B0

// OE / EN
#define APP_P10_OE_GPIO_Port    GPIOB
#define APP_P10_OE_Pin          GPIO_PIN_1   // P5: B1

// Address A/B
#define APP_P10_A_GPIO_Port     GPIOA
#define APP_P10_A_Pin           GPIO_PIN_3   // P4: A3

#define APP_P10_B_GPIO_Port     GPIOA
#define APP_P10_B_Pin           GPIO_PIN_0   // P4: A0

// C hatti (P4 uzerinde PC0 etiketi gorunur, uygunsa kullan):
// (ESKI hali asagida yorumda duruyor)
#define APP_P10_C_GPIO_Port     GPIOC
#define APP_P10_C_Pin           GPIO_PIN_0

/*
ESKI (yorumlu) blok:
#define APP_P10_C_GPIO_Port     GPIOC
#define APP_P10_C_Pin           GPIO_PIN_0
*/

/* Guvenlik: C aktifken C pinleri tanimli olmalı */
#if APP_P10_HAS_C
#ifndef APP_P10_C_GPIO_Port
#error "APP_P10_HAS_C=1 ama APP_P10_C_GPIO_Port tanimli degil"
#endif
#ifndef APP_P10_C_Pin
#error "APP_P10_HAS_C=1 ama APP_P10_C_Pin tanimli degil"
#endif
#endif

