#include "app_log.h"
#include "app_regs.h"
#include "app_config.h"
#include "app_supervisor.h"

#include "cmsis_os.h"

#include "stm32f4xx_hal.h"

#include "fatfs.h"
#include "ff.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
  uint32_t tick_ms;
  uint16_t minutes;
  uint16_t seconds;
} log_evt_t;

static osMessageQueueId_t g_log_q;
static FATFS g_fs;
static uint8_t g_fs_mounted = 0;

static FIL g_file;
static uint8_t g_file_open = 0;
static uint16_t g_open_y = 0;
static uint8_t g_open_m = 0;
static uint8_t g_open_d = 0;

static bool date_valid(uint16_t y, uint8_t m, uint8_t d)
{
  if (y < 2000 || y > 2099) return false;
  if (m < 1 || m > 12) return false;
  if (d < 1 || d > 31) return false;
  return true;
}

static void close_file(void)
{
  if (!g_file_open) return;
  (void)f_sync(&g_file);
  (void)f_close(&g_file);
  g_file_open = 0;
}

static void ensure_log_dir(void)
{
  // logs/ klasoru yoksa olustur (FR_EXIST kabul)
  FRESULT fr = f_mkdir(APP_LOG_DIR);
  if (fr == FR_EXIST) fr = FR_OK;
  (void)fr;
}

static bool open_daily_file(uint16_t y, uint8_t m, uint8_t d)
{
  if (g_file_open && g_open_y == y && g_open_m == m && g_open_d == d) {
    return true;
  }

  close_file();

  ensure_log_dir();

  char path[64];
  snprintf(path, sizeof(path), "%s/%04u-%02u-%02u.csv", APP_LOG_DIR, (unsigned)y, (unsigned)m, (unsigned)d);

  FRESULT fr = f_open(&g_file, path, FA_OPEN_ALWAYS | FA_WRITE);
  if (fr != FR_OK) {
    return false;
  }
  // append mode
  (void)f_lseek(&g_file, f_size(&g_file));

  // header (only if new/empty file)
  if (f_size(&g_file) == 0) {
#if (APP_LOG_PAYLOAD_COUNT_HR == 10) && (APP_LOG_PAYLOAD_START_HR == 6)
    static const char hdr[] = "tick_ms,minutes,seconds,hr6,hr7,hr8,hr9,hr10,hr11,hr12,hr13,hr14,hr15\r\n";
#else
    // generic header
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr), "tick_ms,minutes,seconds");
    for (uint16_t i = 0; i < APP_LOG_PAYLOAD_COUNT_HR; ++i) {
      n += snprintf(hdr + n, sizeof(hdr) - (size_t)n, ",hr%u", (unsigned)(APP_LOG_PAYLOAD_START_HR + i));
    }
    n += snprintf(hdr + n, sizeof(hdr) - (size_t)n, "\r\n");
#endif
    UINT bw = 0;
#if (APP_LOG_PAYLOAD_COUNT_HR == 10) && (APP_LOG_PAYLOAD_START_HR == 6)
    (void)f_write(&g_file, hdr, sizeof(hdr) - 1, &bw);
#else
    (void)f_write(&g_file, hdr, (UINT)strlen(hdr), &bw);
#endif
    (void)bw;
    (void)f_sync(&g_file);
  }

  g_file_open = 1;
  g_open_y = y; g_open_m = m; g_open_d = d;
  return true;
}

void APP_LogInit(void)
{
  g_log_q = osMessageQueueNew(8, sizeof(log_evt_t), NULL);

  // mount once
  if (f_mount(&g_fs, "", 1) == FR_OK) {
    g_fs_mounted = 1;
    ensure_log_dir();
  } else {
    g_fs_mounted = 0;
  }
}

void APP_LogNotifyTime(uint16_t minutes, uint16_t seconds)
{
  if (!g_log_q) return;
  log_evt_t e;
  e.tick_ms = (uint32_t)HAL_GetTick();
  e.minutes = minutes;
  e.seconds = seconds;
  (void)osMessageQueuePut(g_log_q, &e, 0, 0);
}

void APP_LogTask(void *argument)
{
  (void)argument;

  uint32_t last_sync = HAL_GetTick();

  for (;;) {
    APP_SupervisorKick(APP_KICK_LOG);

    // Kural: Tarih gecersizse log yazma, dosya acma (1970 kirlenmesi bitecek).
    uint16_t y, mo, d;
    APP_RegsGetDate(&y, &mo, &d);

    const bool valid = date_valid(y, (uint8_t)mo, (uint8_t)d);
    const bool enabled = (APP_RegsGetLogEnable() != 0);

    if (!g_fs_mounted || !valid || !enabled) {
      close_file();
    } else {
      (void)open_daily_file(y, (uint8_t)mo, (uint8_t)d);
    }

    // consume log event (non-busy)
    log_evt_t e;
    osStatus_t st = osMessageQueueGet(g_log_q, &e, NULL, APP_LOG_SAMPLE_PERIOD_MS);
    if (st == osOK && g_file_open) {
      uint16_t payload[APP_LOG_PAYLOAD_COUNT_HR];
      memset(payload, 0, sizeof(payload));
      (void)APP_RegsReadHRBlock(APP_LOG_PAYLOAD_START_HR, payload, APP_LOG_PAYLOAD_COUNT_HR);

      char line[256];
      int n = snprintf(line, sizeof(line), "%lu,%u,%u",
                       (unsigned long)e.tick_ms,
                       (unsigned)e.minutes,
                       (unsigned)e.seconds);
      for (uint16_t i = 0; i < APP_LOG_PAYLOAD_COUNT_HR; ++i) {
        n += snprintf(line + n, sizeof(line) - (size_t)n, ",%u", (unsigned)payload[i]);
      }
      n += snprintf(line + n, sizeof(line) - (size_t)n, "\r\n");

      UINT bw = 0;
      (void)f_write(&g_file, line, (UINT)strlen(line), &bw);
      (void)bw;
    }

    // periodic sync
    uint32_t now = HAL_GetTick();
    if (g_file_open && (now - last_sync) >= APP_LOG_SYNC_PERIOD_MS) {
      (void)f_sync(&g_file);
      last_sync = now;
    }
  }
}
