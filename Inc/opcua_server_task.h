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

#include "cmsis_os.h"   /* osThreadId_t / osMutexId_t if CMSIS-RTOS2 in use */

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
