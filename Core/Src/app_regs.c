#include "app_regs.h"
#include <string.h>

/* Holding registers */
static uint16_t g_hr[APP_MODBUS_HR_COUNT];
static osMutexId_t g_hr_mutex;

/* Change detection for MMM/SS */
static uint16_t s_last_m = 0xFFFF;
static uint16_t s_last_s = 0xFFFF;
static uint8_t  s_time_dirty = 1;

static uint8_t valid_addr(uint16_t addr)
{
  return (addr < APP_MODBUS_HR_COUNT);
}

/* Must be called while mutex is held */
static void mark_time_dirty_locked(void)
{
  const uint16_t m = g_hr[APP_HR_MINUTES];
  const uint16_t s = g_hr[APP_HR_SECONDS];

  if (m != s_last_m || s != s_last_s) {
    s_time_dirty = 1;
  }
}

void APP_RegsInit(void)
{
  const osMutexAttr_t attr = { .name = "hr_mutex" };
  g_hr_mutex = osMutexNew(&attr);

  memset(g_hr, 0, sizeof(g_hr));
  g_hr[APP_HR_MINUTES]    = 0;
  g_hr[APP_HR_SECONDS]    = 0;
  g_hr[APP_HR_YEAR]       = 1970;
  g_hr[APP_HR_MONTH]      = 1;
  g_hr[APP_HR_DAY]        = 1;
  g_hr[APP_HR_LOG_ENABLE] = 1;

  /* init change tracking */
  s_last_m = 0xFFFF;
  s_last_s = 0xFFFF;
  s_time_dirty = 1;
}

uint16_t APP_RegsReadHR(uint16_t addr)
{
  if (!valid_addr(addr)) return 0;

  osMutexAcquire(g_hr_mutex, osWaitForever);
  uint16_t v = g_hr[addr];
  osMutexRelease(g_hr_mutex);

  return v;
}

uint16_t APP_RegsReadHRBlock(uint16_t addr, uint16_t* out, uint16_t qty)
{
  if (!out) return 0;
  if (qty == 0) return 0;
  if ((uint32_t)addr + (uint32_t)qty > (uint32_t)APP_MODBUS_HR_COUNT) return 0;

  osMutexAcquire(g_hr_mutex, osWaitForever);
  for (uint16_t i = 0; i < qty; ++i) out[i] = g_hr[addr + i];
  osMutexRelease(g_hr_mutex);

  return qty;
}

uint16_t APP_RegsWriteHR(uint16_t addr, uint16_t value)
{
  if (!valid_addr(addr)) return 0;

  osMutexAcquire(g_hr_mutex, osWaitForever);
  g_hr[addr] = value;

  /* if time fields touched -> mark dirty */
  if (addr == APP_HR_MINUTES || addr == APP_HR_SECONDS) {
    mark_time_dirty_locked();
  }

  osMutexRelease(g_hr_mutex);
  return 1;
}

uint16_t APP_RegsWriteHRBlock(uint16_t addr, const uint16_t* in, uint16_t qty)
{
  if (!in) return 0;
  if (qty == 0) return 0;
  if ((uint32_t)addr + (uint32_t)qty > (uint32_t)APP_MODBUS_HR_COUNT) return 0;

  osMutexAcquire(g_hr_mutex, osWaitForever);

  for (uint16_t i = 0; i < qty; ++i) g_hr[addr + i] = in[i];

  /* If block overlaps MMM/SS -> mark dirty */
  const uint32_t a = (uint32_t)addr;
  const uint32_t e = a + (uint32_t)qty; /* [a,e) */
  if ((a <= (uint32_t)APP_HR_MINUTES && e > (uint32_t)APP_HR_MINUTES) ||
      (a <= (uint32_t)APP_HR_SECONDS && e > (uint32_t)APP_HR_SECONDS))
  {
    mark_time_dirty_locked();
  }

  osMutexRelease(g_hr_mutex);
  return qty;
}

void APP_RegsGetTime(uint16_t* minutes, uint16_t* seconds)
{
  if (!minutes || !seconds) return;

  osMutexAcquire(g_hr_mutex, osWaitForever);
  *minutes = g_hr[APP_HR_MINUTES];
  *seconds = g_hr[APP_HR_SECONDS];
  osMutexRelease(g_hr_mutex);
}

void APP_RegsGetDate(uint16_t* year, uint16_t* month, uint16_t* day)
{
  if (!year || !month || !day) return;

  osMutexAcquire(g_hr_mutex, osWaitForever);
  *year  = g_hr[APP_HR_YEAR];
  *month = g_hr[APP_HR_MONTH];
  *day   = g_hr[APP_HR_DAY];
  osMutexRelease(g_hr_mutex);
}

uint8_t APP_RegsGetLogEnable(void)
{
  osMutexAcquire(g_hr_mutex, osWaitForever);
  uint16_t v = g_hr[APP_HR_LOG_ENABLE];
  osMutexRelease(g_hr_mutex);
  return (v != 0);
}

bool APP_RegsConsumeChangedTime(uint16_t *mmm, uint16_t *ss)
{
  bool changed = false;

  osMutexAcquire(g_hr_mutex, osWaitForever);

  const uint16_t m = g_hr[APP_HR_MINUTES];
  const uint16_t s = g_hr[APP_HR_SECONDS];

  if (s_time_dirty || m != s_last_m || s != s_last_s) {
    s_time_dirty = 0;
    s_last_m = m;
    s_last_s = s;

    if (mmm) *mmm = m;
    if (ss)  *ss  = s;

    changed = true;
  }

  osMutexRelease(g_hr_mutex);
  return changed;
}
