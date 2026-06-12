/*
 * opcua_server_task.h
 *
 * FreeRTOS task that hosts the open62541 OPC UA server on the STM32F407
 * gateway.  A single dedicated task drives UA_Server_run_iterate() in a
 * tick-driven loop, so the server shares the lwIP thread-safety model
 * with the rest of the application.
 */

#ifndef OPCUA_SERVER_TASK_H_
#define OPCUA_SERVER_TASK_H_

#include <stdint.h>

/* CMSIS-RTOS2 wrappers.  In your real STM32CubeIDE project this comes
 * from the CMSIS pack; on the CI build we stub it so the source still
 * compiles against plain arm-none-eabi-gcc + FreeRTOS.
 */
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
  #include "cmsis_os.h"
#else
  #include "cmsis_os_stubs.h"
#endif

/* ----------------------------------------------------------------------------
 * Public configuration
 * ---------------------------------------------------------------------------- */
#define OPCUA_TASK_STACK_BYTES   (16 * 1024UL)   /* 16 KB, monitor with uxTaskGetStackHighWaterMark */
#define OPCUA_TASK_PRIORITY      (osPriorityAboveNormal)
#define OPCUA_TASK_PERIOD_MS     50              /* 20 Hz server tick       */

#define OPCUA_SERVER_PORT        4840

/* Mutex used to protect hardware register access shared with Modbus / ISR. */
extern osMutexId_t           g_IoRegMutexHandle;
extern const osMutexAttr_t   g_IoRegMutex_attr;

/* ----------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------- */

/* Called once from main() / default task BEFORE the RTOS scheduler starts,
 * or from a startup task.  Sets up lwIP, the open62541 server, the node
 * model, and creates the OPC UA server task.
 *
 * Returns 0 on success, negative on failure.
 */
int32_t OpcUaServer_Init(void);

/* The FreeRTOS task entry point.  Public so the task descriptor can hook it. */
void    vOpcUaServerTask(void *argument);

/* Graceful shutdown - call from a fault handler if needed. */
void    OpcUaServer_Stop(void);

/* Helpers exposed for the node-model callbacks (read/write hardware
 * registers in a thread-safe way).  They are also used by the Modbus
 * glue code, so they live here rather than in opcua_node_model.c.
 */
uint8_t  OpcUa_Hw_ReadDI (uint8_t channel);
void     OpcUa_Hw_WriteDO(uint8_t channel, uint8_t value);
int32_t  OpcUa_Hw_ReadAI (uint8_t channel);
void     OpcUa_Hw_WriteAO(uint8_t channel, int32_t value);
uint8_t  OpcUa_Hw_ReadRelay (uint8_t channel);
void     OpcUa_Hw_WriteRelay(uint8_t channel, uint8_t value);

#endif /* OPCUA_SERVER_TASK_H_ */
