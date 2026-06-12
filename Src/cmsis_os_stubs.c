/*
 * cmsis_os_stubs.c
 *
 * Minimal stubs for the CMSIS-RTOS2 symbols used by opcua_server_task.c
 * and opcua_node_model.c.  These are compiled ONLY on the CI build
 * (where CMSIS-RTOS2 is not available) and never on the STM32 target,
 * where the real CMSIS pack is linked in instead.
 *
 * They are no-ops - good enough to satisfy the linker and prove that
 * the application translation units are syntactically and semantically
 * valid on a host ARM cross compiler.  The actual FreeRTOS task is
 * driven from main() on the real target, not by these stubs.
 */

#include <stdint.h>
#include <stddef.h>

#include "cmsis_os_stubs.h"

osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
    (void)attr;
    return (osMutexId_t)0x1;   /* any non-NULL handle */
}

osStatus_t osMutexAcquire(osMutexId_t m, uint32_t t)
{
    (void)m; (void)t;
    return 0;   /* osOK */
}

osStatus_t osMutexRelease(osMutexId_t m)
{
    (void)m;
    return 0;
}

osThreadId_t osThreadGetId(void)
{
    return (osThreadId_t)0x1;
}

osStatus_t osThreadTerminate(osThreadId_t id)
{
    (void)id;
    for (;;) { /* halt */ }
    return 0;
}

osThreadId_t osThreadNew(osThreadFunc_t fn, void *arg, const osThreadAttr_t *attr)
{
    (void)attr;
    if (fn) fn(arg);
    return (osThreadId_t)0x1;
}

void osDelay(uint32_t ms)
{
    (void)ms;
}
