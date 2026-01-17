#ifndef APP_REGS_H
#define APP_REGS_H

#include <stdint.h>
#include "cmsis_os.h"
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

void APP_RegsInit(void);
uint16_t APP_RegsReadHR(uint16_t addr);
uint16_t APP_RegsReadHRBlock(uint16_t addr, uint16_t* out, uint16_t qty);
uint16_t APP_RegsWriteHR(uint16_t addr, uint16_t value);
uint16_t APP_RegsWriteHRBlock(uint16_t addr, const uint16_t* in, uint16_t qty);

void APP_RegsGetTime(uint16_t* minutes, uint16_t* seconds);
void APP_RegsGetDate(uint16_t* year, uint16_t* month, uint16_t* day);
uint8_t APP_RegsGetLogEnable(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_REGS_H */
