#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void APP_WdgInit(uint32_t timeout_ms);
void APP_WdgKick(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_WATCHDOG_H */
