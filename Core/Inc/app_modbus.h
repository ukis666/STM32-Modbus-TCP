#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include <stdint.h>

#ifndef APP_MODBUS_TCP_PORT
#define APP_MODBUS_TCP_PORT 502
#endif

void APP_ModbusTask(void *argument);

#endif
