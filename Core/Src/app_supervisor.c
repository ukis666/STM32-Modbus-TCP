#include "app_supervisor.h"
#include "cmsis_os.h"
#include "app_config.h"
#include "app_watchdog.h"

static uint32_t g_last_kick[APP_KICK_MAX];

void APP_SupervisorInit(void)
{
  uint32_t now = osKernelGetTickCount();
  for (int i = 0; i < APP_KICK_MAX; ++i) g_last_kick[i] = now;
}

void APP_SupervisorKick(app_kick_source_t src)
{
  if ((int)src < 0 || src >= APP_KICK_MAX) return;
  g_last_kick[src] = osKernelGetTickCount();
}

void APP_SupervisorTask(void *argument)
{
  (void)argument;
  const uint32_t max_age_ms = (APP_WDG_TIMEOUT_MS > 2000) ? (APP_WDG_TIMEOUT_MS - 1000) : 2000;

  for (;;) {
    uint32_t now = osKernelGetTickCount();
    uint8_t ok = 1;
    for (int i = 0; i < APP_KICK_MAX; ++i) {
      uint32_t age = now - g_last_kick[i];
      if (age > max_age_ms) { ok = 0; break; }
    }

    if (ok) {
      APP_WdgKick();
    }

    osDelay(250);
  }
}
