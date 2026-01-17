#include "app_watchdog.h"
#include "stm32f4xx.h"

/*
 * IWDG register-level init.
 * LSI frekansi sahaya gore degisebilir; bu nedenle timeout toleransli secildi.
 */

static uint8_t g_wdg_started = 0;

void APP_WdgInit(uint32_t timeout_ms)
{
  if (g_wdg_started) return;

  /* LSI ~32kHz. Prescaler 64 -> tick ~500Hz. */
  const uint32_t presc = 3; /* PR=3 => /64 */
  const uint32_t lsi_hz = 32000;
  const uint32_t tick_hz = lsi_hz / 64;
  uint32_t reload = (timeout_ms * tick_hz) / 1000;
  if (reload < 1) reload = 1;
  if (reload > 0x0FFF) reload = 0x0FFF;

  IWDG->KR = 0x5555;          /* Enable write access */
  IWDG->PR = presc;
  IWDG->RLR = reload;
  IWDG->KR = 0xAAAA;          /* Reload */
  IWDG->KR = 0xCCCC;          /* Start */

  g_wdg_started = 1;
}

void APP_WdgKick(void)
{
  if (!g_wdg_started) return;
  IWDG->KR = 0xAAAA;
}
