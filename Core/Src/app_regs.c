#include "app_regs.h"
#include <string.h>

static uint16_t g_hr[APP_MODBUS_HR_COUNT];
static osMutexId_t g_hr_mutex;

void APP_RegsInit(void)
{
  const osMutexAttr_t attr = { .name = "hr_mutex" };
  g_hr_mutex = osMutexNew(&attr);

  memset(g_hr, 0, sizeof(g_hr));
  g_hr[APP_HR_MINUTES] = 0;
  g_hr[APP_HR_SECONDS] = 0;
  g_hr[APP_HR_YEAR] = 1970;
  g_hr[APP_HR_MONTH] = 1;
  g_hr[APP_HR_DAY] = 1;
  g_hr[APP_HR_LOG_ENABLE] = 1;
}

static uint8_t valid_addr(uint16_t addr)
{
  return (addr < APP_MODBUS_HR_COUNT);
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
  if ((uint32_t)addr + qty > APP_MODBUS_HR_COUNT) return 0;

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
  osMutexRelease(g_hr_mutex);
  return 1;
}

uint16_t APP_RegsWriteHRBlock(uint16_t addr, const uint16_t* in, uint16_t qty)
{
  if (!in) return 0;
  if (qty == 0) return 0;
  if ((uint32_t)addr + qty > APP_MODBUS_HR_COUNT) return 0;

  osMutexAcquire(g_hr_mutex, osWaitForever);
  for (uint16_t i = 0; i < qty; ++i) g_hr[addr + i] = in[i];
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
