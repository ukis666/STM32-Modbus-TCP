#include "app_system.h"
#include "app_config.h"
#include "app_regs.h"
#include "app_modbus.h"
#include "app_log.h"
#include "app_p10.h"
#include "app_supervisor.h"
#include "app_watchdog.h"

#include "cmsis_os.h"

static osThreadId_t g_modbus_task;
static osThreadId_t g_log_task;
static osThreadId_t g_p10_task;
static osThreadId_t g_sup_task;

void APP_SystemEarlyInit(void)
{
  /* GPIO safe init for P10 */
  APP_P10_Init();
}

void APP_SystemStart(void)
{
  /* Init shared state */
  APP_RegsInit();
  APP_LogInit();
  APP_SupervisorInit();

  /* Start watchdog */
  APP_WdgInit(APP_WDG_TIMEOUT_MS);

  const osThreadAttr_t modbus_attr = { .name = "modbus", .stack_size = 1024, .priority = (osPriority_t)osPriorityAboveNormal };
  const osThreadAttr_t log_attr    = { .name = "log",    .stack_size = 1536, .priority = (osPriority_t)osPriorityNormal };
  const osThreadAttr_t p10_attr    = { .name = "p10",    .stack_size = 1024, .priority = (osPriority_t)osPriorityHigh };
  const osThreadAttr_t sup_attr    = { .name = "sup",    .stack_size = 768,  .priority = (osPriority_t)osPriorityAboveNormal };

  g_modbus_task = osThreadNew(APP_ModbusTask, NULL, &modbus_attr);
  g_log_task    = osThreadNew(APP_LogTask, NULL, &log_attr);
  g_p10_task    = osThreadNew(APP_P10_Task, NULL, &p10_attr);
  g_sup_task    = osThreadNew(APP_SupervisorTask, NULL, &sup_attr);

  (void)g_modbus_task;
  (void)g_log_task;
  (void)g_p10_task;
  (void)g_sup_task;
}
