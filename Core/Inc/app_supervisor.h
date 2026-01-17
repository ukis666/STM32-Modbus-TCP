#ifndef APP_SUPERVISOR_H
#define APP_SUPERVISOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  APP_KICK_MODBUS = 0,
  APP_KICK_LOG    = 1,
  APP_KICK_P10    = 2,
  APP_KICK_MAX
} app_kick_source_t;

void APP_SupervisorInit(void);
void APP_SupervisorKick(app_kick_source_t src);
void APP_SupervisorTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* APP_SUPERVISOR_H */
