#include "app_log.h"
#include "app_config.h"
#include "app_regs.h"
#include "app_supervisor.h"

#include "cmsis_os.h"
#include "fatfs.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  uint32_t tick;
  uint16_t minutes;
  uint16_t seconds;
} log_event_t;

static osMessageQueueId_t g_log_q;
static FIL g_file;
static uint8_t g_file_open = 0;
static uint16_t g_cur_y=0, g_cur_m=0, g_cur_d=0;
static uint32_t g_last_sync = 0;

static void make_filename(char* out, size_t cap, uint16_t y, uint16_t m, uint16_t d)
{
  if (cap < 32) return;
  snprintf(out, cap, "%s/%04u-%02u-%02u.csv", APP_LOG_DIR, (unsigned)y, (unsigned)m, (unsigned)d);
}

static uint8_t date_valid(uint16_t y, uint16_t m, uint16_t d)
{
  if (y < 2000 || y > 2099) return 0;
  if (m < 1 || m > 12) return 0;
  if (d < 1 || d > 31) return 0;
  return 1;
}

static FRESULT ensure_dir(const char* dir)
{
  FRESULT fr = f_mkdir(dir);
  if (fr == FR_EXIST) fr = FR_OK;
  return fr;
}

static void close_file(void)
{
  if (g_file_open) {
    f_sync(&g_file);
    f_close(&g_file);
    g_file_open = 0;
  }
}

static FRESULT open_daily_file(uint16_t y, uint16_t m, uint16_t d)
{
  char fn[64];
  make_filename(fn, sizeof(fn), y, m, d);

  ensure_dir(APP_LOG_DIR);

  /* Yeni dosya mi kontrol et */
  FILINFO fno;
  uint8_t exists = (f_stat(fn, &fno) == FR_OK);

  close_file();

  FRESULT fr = f_open(&g_file, fn, FA_OPEN_ALWAYS | FA_WRITE);
  if (fr != FR_OK) return fr;
  g_file_open = 1;

  /* Append */
  f_lseek(&g_file, f_size(&g_file));

  if (!exists) {
    const char* hdr = "tick_ms,minutes,seconds\r\n";
    UINT bw;
    f_write(&g_file, hdr, (UINT)strlen(hdr), &bw);
  }

  g_cur_y=y; g_cur_m=m; g_cur_d=d;
  g_last_sync = osKernelGetTickCount();
  return FR_OK;
}

void APP_LogInit(void)
{
  const osMessageQueueAttr_t attr = { .name = "log_q" };
  g_log_q = osMessageQueueNew(16, sizeof(log_event_t), &attr);
}

void APP_LogNotifyTime(uint16_t minutes, uint16_t seconds)
{
  if (!g_log_q) return;
  log_event_t e;
  e.tick = osKernelGetTickCount();
  e.minutes = minutes;
  e.seconds = seconds;
  (void)osMessageQueuePut(g_log_q, &e, 0, 0);
}

void APP_LogTask(void *argument)
{
  (void)argument;

  /* Mount FS */
  if (f_mount(&SDFatFS, (TCHAR const*)SDPath, 1) != FR_OK) {
    /* SD yoksa task yine calissin, supervisor beslesin */
  }

  for (;;) {
    log_event_t e;
    osStatus_t st = osMessageQueueGet(g_log_q, &e, NULL, APP_LOG_SAMPLE_PERIOD_MS);

    uint16_t y,m,d;
    APP_RegsGetDate(&y,&m,&d);
    if (!date_valid(y,m,d)) { y=1970; m=1; d=1; }

    if (!g_file_open || y!=g_cur_y || m!=g_cur_m || d!=g_cur_d) {
      open_daily_file(y,m,d);
    }

    if (st == osOK) {
      if (APP_RegsGetLogEnable() && g_file_open) {
        char line[64];
        int n = snprintf(line, sizeof(line), "%lu,%u,%u\r\n", (unsigned long)e.tick, (unsigned)e.minutes, (unsigned)e.seconds);
        if (n > 0) {
          UINT bw;
          f_write(&g_file, line, (UINT)n, &bw);
        }
      }
    }

    /* Periodic sync */
    uint32_t now = osKernelGetTickCount();
    if (g_file_open && (now - g_last_sync) >= APP_LOG_SYNC_PERIOD_MS) {
      f_sync(&g_file);
      g_last_sync = now;
    }

    APP_SupervisorKick(APP_KICK_LOG);
  }
}
