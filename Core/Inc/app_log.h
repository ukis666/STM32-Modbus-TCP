#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void APP_LogInit(void);
void APP_LogNotifyTime(uint16_t minutes, uint16_t seconds);
void APP_LogTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOG_H */
