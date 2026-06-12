/*
 * opcua_server_task.h
 *
 * FreeRTOS task that hosts the open62541 OPC UA server on the STM32F407
 * gateway.  A single dedicated task drives UA_Server_run_iterate() in a
 * tick-driven loop, so the server shares the lwIP thread-safety model
 * with the rest of the application.
 *
 * The public surface uses simple portable types (uint32_t, void*) so the
 * file compiles equally well against:
 *   - the real CMSIS-RTOS2 + FreeRTOS + open62541 (STM32CubeIDE)
 *   - the local CMSIS-RTOS2 stubs + libc + open62541_stub.h (CI build)
 *
 * Define OPCUA_EMBEDDED_TARGET=1 on the STM32CubeIDE build.
 */

#ifndef OPCUA_SERVER_TASK_H_
#define OPCUA_SERVER_TASK_H_

#include <stdint.h>

/* ----------------------------------------------------------------------------
 * OS abstraction (mutex + thread + delay)
 * ---------------------------------------------------------------------------- */
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
  #include "cmsis_os.h"
  typedef osThreadId_t OpcUaThreadId;
  typedef osMutexId_t  OpcUaMutexId;
  typedef osStatus_t   OpcUaStatus;
  typedef osPriority_t OpcUaPriority;
  typedef osThreadAttr_t OpcUaThreadAttr;
  #define OPCUA_OS_WAIT_FOREVER  osWaitForever
  static inline OpcUaStatus OpcUa_MutexAcquire(OpcUaMutexId m, uint32_t t) { return osMutexAcquire(m, t); }
  static inline OpcUaStatus OpcUa_MutexRelease(OpcUaMutexId m)            { return osMutexRelease(m); }
#else
  #include "cmsis_os_stubs.h"
  typedef osThreadId_t    OpcUaThreadId;
  typedef osMutexId_t     OpcUaMutexId;
  typedef osStatus_t      OpcUaStatus;
  typedef osPriority_t    OpcUaPriority;
  /* Portable struct for the thread attributes; the real CMSIS version
   * also has this layout (name / stack_size / priority / cb_mem / cb_size). */
  typedef struct {
    const char *name;
    uint32_t    stack_size;
    int         priority;
    void       *cb_mem;
    uint32_t    cb_size;
  } OpcUaThreadAttr;
  #define OPCUA_OS_WAIT_FOREVER  0xFFFFFFFFU
  /* On the CI host build we don't have a real mutex - the wrappers just
   * delegate to the no-op stubs in cmsis_os_stubs.c. */
  static inline OpcUaStatus OpcUa_MutexAcquire(OpcUaMutexId m, uint32_t t) { (void)m; (void)t; return 0; }
  static inline OpcUaStatus OpcUa_MutexRelease(OpcUaMutexId m)            { (void)m; return 0; }
#endif

/* ----------------------------------------------------------------------------
 * open62541 header
 * ---------------------------------------------------------------------------- */
/* Both CI and target builds consume the same open62541.h header; only the
 * includes for the embedded-only stack (lwIP, FreeRTOS, CMSIS) are guarded. */
#include "open62541.h"

/* ----------------------------------------------------------------------------
 * Public configuration
 * ---------------------------------------------------------------------------- */
#define OPCUA_TASK_STACK_BYTES   (16 * 1024UL)   /* 16 KB, monitor with uxTaskGetStackHighWaterMark */
#define OPCUA_TASK_PERIOD_MS     50              /* 20 Hz server tick       */
#define OPCUA_SERVER_PORT        14840   /* TEST: should be 14840 if my macro wins */

/* Thread-safe IO helpers exposed for the node-model callbacks. */
uint8_t  OpcUa_Hw_ReadDI   (uint8_t channel);
void     OpcUa_Hw_WriteDO  (uint8_t channel, uint8_t value);
int32_t  OpcUa_Hw_ReadAI   (uint8_t channel);
void     OpcUa_Hw_WriteAO  (uint8_t channel, int32_t value);
uint8_t  OpcUa_Hw_ReadRelay (uint8_t channel);
void     OpcUa_Hw_WriteRelay(uint8_t channel, uint8_t value);

/* Server lifecycle. */
int32_t  OpcUaServer_Init (void);
void     vOpcUaServerTask(void *argument);
void     OpcUaServer_Stop (void);

/* Return the live UA_Server* (only meaningful after OpcUaServer_Init). */
void    *OpcUaServer_GetHandle(void);

#endif /* OPCUA_SERVER_TASK_H_ */
