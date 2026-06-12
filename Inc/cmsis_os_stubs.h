/*
 * cmsis_os_stubs.h
 *
 * Local copy of the CMSIS-RTOS2 types used by the application, used
 * ONLY by the CI build where the real CMSIS pack is not present.
 * Define OPCUA_USE_CMSIS_OS=1 on the STM32CubeIDE build to use the
 * real cmsis_os.h instead.
 */

#ifndef CMSIS_OS_STUBS_H_
#define CMSIS_OS_STUBS_H_

#include <stdint.h>
#include <stddef.h>

typedef void *osThreadId_t;
typedef void *osMutexId_t;
typedef struct {
    const char *name;
    int         attr_bits;
    void       *cb_mem;
    uint32_t    cb_size;
} osMutexAttr_t;
typedef int  osPriority_t;
typedef int  osStatus_t;
typedef void *osThreadAttr_t;
typedef void (*osThreadFunc_t)(void *);

#define osPriorityAboveNormal  ((osPriority_t) 2)
#define osMutexRecursive        0x01
#define osMutexPrioInherit      0x02
#define osWaitForever           0xFFFFFFFFU

osMutexId_t   osMutexNew      (const osMutexAttr_t *attr);
osStatus_t    osMutexAcquire  (osMutexId_t m, uint32_t timeout);
osStatus_t    osMutexRelease  (osMutexId_t m);
osThreadId_t  osThreadGetId   (void);
osStatus_t    osThreadTerminate(osThreadId_t id);
osThreadId_t  osThreadNew     (osThreadFunc_t fn, void *arg,
                               const osThreadAttr_t *attr);
void          osDelay         (uint32_t ms);

#endif /* CMSIS_OS_STUBS_H_ */
