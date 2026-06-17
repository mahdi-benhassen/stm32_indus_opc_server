/*
 * opcua_server_task.c
 *
 * FreeRTOS task that drives the open62541 v1.5.4 server on top of lwIP.
 *
 *   main() / startup
 *       |
 *       v
 *   OpcUaServer_Init()  -- creates the IO mutex, the open62541 server
 *                          and the address space, then spawns
 *                          vOpcUaServerTask.
 *
 *   vOpcUaServerTask
 *       UA_Server_run_startup(server)
 *       for(;;) {
 *           UA_Server_run_iterate(server, true);  // wait for messages
 *           osDelay(OPCUA_TASK_PERIOD_MS);
 *           IWDG_Refresh();                        // kick the watchdog
 *       }
 */

#include "opcua_server_task.h"
#include "opcua_node_model.h"
#include "opcua_access_control.h"

#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
  #include "lwip/sockets.h"
  #include "lwip/netif.h"
  #include "FreeRTOS.h"
  #include "cmsis_os.h"
  #include <stdio.h>
  #include "stm32f4xx_hal.h"   /* HAL_IWDG_Refresh */
#endif

/* ----------------------------------------------------------------------------
 * Globals
 * ---------------------------------------------------------------------------- */
static UA_Server         *g_opcuaServer     = NULL;
static OpcUaThreadId      g_opcuaTaskHandle  = NULL;
static volatile uint8_t   g_opcuaRunning     = 0;

#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
static const osMutexAttr_t g_IoRegMutex_attr = {
    .name      = "IoRegMutex",
    .attr_bits = osMutexRecursive | osMutexPrioInherit,
    .cb_mem    = NULL,
    .cb_size   = 0U
};
static OpcUaMutexId g_IoRegMutexHandle;
#else
static OpcUaMutexId g_IoRegMutexHandle = (OpcUaMutexId)0;
#endif

/* ----------------------------------------------------------------------------
 * Shared I/O register shadow
 *
 * Modbus and the OPC UA server both need a single, consistent view of the
 * hardware I/O.  This shadow is updated by the drivers (ISR or Modbus task)
 * and sampled by the OPC UA callbacks under the IO mutex.
 * ---------------------------------------------------------------------------- */
typedef struct {
    uint8_t  di[8];
    uint8_t  do_[8];
    int32_t  ai[8];
    int32_t  ao[8];
    uint8_t  relay[4];
    uint32_t counter;
} IoShadow_t;

static IoShadow_t s_shadow;

/* ----------------------------------------------------------------------------
 * Thread-safe accessors
 *
 * All shadow reads/writes are protected by g_IoRegMutexHandle.  Hardware
 * driver calls happen OUTSIDE the mutex to keep the critical section short,
 * EXCEPT for EmergencyStop which holds the mutex across all writes to
 * guarantee atomicity.
 * ---------------------------------------------------------------------------- */
uint8_t OpcUa_Hw_ReadDI(uint8_t ch)
{
    if (ch >= 8) return 0;
    OpcUa_MutexAcquire(g_IoRegMutexHandle, OPCUA_OS_WAIT_FOREVER);
    uint8_t v = s_shadow.di[ch];
    OpcUa_MutexRelease(g_IoRegMutexHandle);
    return v;
}

void OpcUa_Hw_WriteDO(uint8_t ch, uint8_t v)
{
    if (ch >= 8) return;
    OpcUa_MutexAcquire(g_IoRegMutexHandle, OPCUA_OS_WAIT_FOREVER);
    s_shadow.do_[ch] = (v ? 1 : 0);
    OpcUa_MutexRelease(g_IoRegMutexHandle);
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
    extern void DO_Driver_Set(uint8_t channel, uint8_t value);
    DO_Driver_Set(ch, v ? 1 : 0);
#else
    (void)ch; (void)v;
#endif
}

int32_t OpcUa_Hw_ReadAI(uint8_t ch)
{
    if (ch >= 8) return 0;
    OpcUa_MutexAcquire(g_IoRegMutexHandle, OPCUA_OS_WAIT_FOREVER);
    int32_t v = s_shadow.ai[ch];
    OpcUa_MutexRelease(g_IoRegMutexHandle);
    return v;
}

void OpcUa_Hw_WriteAO(uint8_t ch, int32_t v)
{
    if (ch >= 8) return;
    OpcUa_MutexAcquire(g_IoRegMutexHandle, OPCUA_OS_WAIT_FOREVER);
    s_shadow.ao[ch] = v;
    OpcUa_MutexRelease(g_IoRegMutexHandle);
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
    extern void AO_Driver_Set(uint8_t channel, int32_t value);
    AO_Driver_Set(ch, v);
#else
    (void)ch; (void)v;
#endif
}

uint8_t OpcUa_Hw_ReadRelay(uint8_t ch)
{
    if (ch >= 4) return 0;
    OpcUa_MutexAcquire(g_IoRegMutexHandle, OPCUA_OS_WAIT_FOREVER);
    uint8_t v = s_shadow.relay[ch];
    OpcUa_MutexRelease(g_IoRegMutexHandle);
    return v;
}

void OpcUa_Hw_WriteRelay(uint8_t ch, uint8_t v)
{
    if (ch >= 4) return;
    OpcUa_MutexAcquire(g_IoRegMutexHandle, OPCUA_OS_WAIT_FOREVER);
    s_shadow.relay[ch] = (v ? 1 : 0);
    OpcUa_MutexRelease(g_IoRegMutexHandle);
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
    extern void RELAY_Driver_Set(uint8_t channel, uint8_t value);
    RELAY_Driver_Set(ch, v ? 1 : 0);
#else
    (void)ch; (void)v;
#endif
}

/* Read back the DO shadow value (used by the OPC UA read callback).
 * Separate from OpcUa_Hw_WriteDO which is the write path. */
uint8_t OpcUa_Hw_ReadDO(uint8_t ch)
{
    if (ch >= 8) return 0;
    OpcUa_MutexAcquire(g_IoRegMutexHandle, OPCUA_OS_WAIT_FOREVER);
    uint8_t v = s_shadow.do_[ch];
    OpcUa_MutexRelease(g_IoRegMutexHandle);
    return v;
}

/* Read back the AO shadow value (used by the OPC UA read callback). */
int32_t OpcUa_Hw_ReadAO(uint8_t ch)
{
    if (ch >= 8) return 0;
    OpcUa_MutexAcquire(g_IoRegMutexHandle, OPCUA_OS_WAIT_FOREVER);
    int32_t v = s_shadow.ao[ch];
    OpcUa_MutexRelease(g_IoRegMutexHandle);
    return v;
}

/* ----------------------------------------------------------------------------
 * Atomic emergency-stop: hold the IO mutex across ALL writes so a
 * concurrent SCADA write cannot leave an output ON between iterations.
 * This is safety-critical — do NOT split into individual OpcUa_Hw_Write
 * calls.
 * ---------------------------------------------------------------------------- */
void OpcUa_Hw_EmergencyStopAll(void)
{
    OpcUa_MutexAcquire(g_IoRegMutexHandle, OPCUA_OS_WAIT_FOREVER);
    for (uint8_t i = 0; i < 8; i++)
        s_shadow.do_[i] = 0;
    for (uint8_t i = 0; i < 4; i++)
        s_shadow.relay[i] = 0;
    OpcUa_MutexRelease(g_IoRegMutexHandle);

#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
    extern void DO_Driver_Set(uint8_t channel, uint8_t value);
    extern void RELAY_Driver_Set(uint8_t channel, uint8_t value);
    for (uint8_t i = 0; i < 8; i++)
        DO_Driver_Set(i, 0);
    for (uint8_t i = 0; i < 4; i++)
        RELAY_Driver_Set(i, 0);
#endif
}

#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
/* ISR-side helpers - only meaningful on the real target.
 * On Cortex-M4, aligned 8-bit and 32-bit stores are atomic, so these
 * can safely update the shadow without the mutex (the ISR cannot block).
 * The OPC UA callbacks sample under the mutex and will pick up the new
 * value on the next read. */
void OpcUa_Hw_UpdateDIFromISR(uint8_t ch, uint8_t v)
{
    if (ch >= 8) return;
    s_shadow.di[ch] = v ? 1 : 0;
}
void OpcUa_Hw_UpdateAIFromISR(uint8_t ch, int32_t v)
{
    if (ch >= 8) return;
    s_shadow.ai[ch] = v;
}
#endif

/* ----------------------------------------------------------------------------
 * open62541 logger - v1.5.4 UA_Logger signature
 *
 * open62541 v1.5.4 uses custom format specifiers (%S for UA_String,
 * %N for NodeId, %Q for QualifiedName) that standard vsnprintf does NOT
 * understand.  We use UA_String_vformat to format the message into a
 * UA_String, then pass the raw bytes to the UART/RTT output.
 * ---------------------------------------------------------------------------- */
static void OpcUa_Log(void *ctx, UA_LogLevel level, UA_LogCategory category,
                      const char *msg, va_list args)
{
    (void)ctx; (void)level; (void)category;
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
    UA_String formatted = UA_STRING_NULL;
    UA_StatusCode r = UA_String_vformat(&formatted, msg, args);
    if (r == UA_STATUSCODE_GOOD && formatted.length > 0) {
        extern void Log_Print(const char *s, uint16_t len);
        Log_Print((const char *)formatted.data, (uint16_t)formatted.length);
    }
    UA_String_clear(&formatted);
#else
    /* CI build: just print to stdout for debugging. */
    vprintf(msg, args);
    printf("\n");
    (void)msg; (void)args;
#endif
}

static UA_Logger g_opcuaLogger = {
    .log     = OpcUa_Log,
    .context = NULL,
    .clear   = NULL
};

/* ----------------------------------------------------------------------------
 * Server initialization
 *
 * We allocate a UA_ServerConfig on the stack, fill it with the requested
 * port + SecurityPolicy#None + logger, then call UA_Server_newWithConfig
 * to create the server.  UA_Server_new() (which auto-creates a config
 * bound to port 4840) would be too late: the event loop would have
 * already started and bound 4840, so a later setMinimal() would
 * merely warn and have no effect.
 *
 * On error, the UA_Server is deleted so no memory is leaked.
 * ---------------------------------------------------------------------------- */
int32_t OpcUaServer_Init(void)
{
    /* 1. Create the IO mutex up front. */
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
    g_IoRegMutexHandle = osMutexNew(&g_IoRegMutex_attr);
    if (!g_IoRegMutexHandle) return -1;
#endif

    /* 2. Build the config first.  setMinimal configures the
     *    event loop, the TCP listener (on the requested port) and
     *    the SecurityPolicy#None. */
    UA_ServerConfig config;
    memset(&config, 0, sizeof(UA_ServerConfig));
    UA_StatusCode r = UA_ServerConfig_setMinimal(&config, OPCUA_SERVER_PORT, NULL);
    if (r != UA_STATUSCODE_GOOD) return -2;

    config.logging = &g_opcuaLogger;

#if OPCUA_ENABLE_ACCESS_CONTROL
    /* Enforce username/password authentication when enabled. */
    OpcUa_AccessControl_Configure(&config);
#endif

    /* 3. Create the server.  This actually starts the event loop
     *    and binds the listener socket on OPCUA_SERVER_PORT. */
    g_opcuaServer = UA_Server_newWithConfig(&config);
    if (!g_opcuaServer) return -3;

    /* 4. Build the address space.  On failure, delete the server
     *    to avoid leaking the UA_Server and its nodestore. */
    if (OpcUaNodeModel_Build(g_opcuaServer) != UA_STATUSCODE_GOOD) {
        UA_Server_delete(g_opcuaServer);
        g_opcuaServer = NULL;
        return -4;
    }

    /* 5. Spawn the FreeRTOS task that drives the server. */
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
    {
        const osThreadAttr_t taskAttr = {
            .name       = "OpcUaSrv",
            .stack_size = OPCUA_TASK_STACK_BYTES,
            .priority   = OPCUA_TASK_PRIORITY,
            .cb_mem     = NULL,
            .cb_size    = 0U
        };
        g_opcuaTaskHandle = osThreadNew(vOpcUaServerTask, NULL, &taskAttr);
        if (!g_opcuaTaskHandle) {
            UA_Server_delete(g_opcuaServer);
            g_opcuaServer = NULL;
            return -5;
        }
    }
#else
    (void)vOpcUaServerTask;
    g_opcuaTaskHandle = (OpcUaThreadId)0;
#endif

    g_opcuaRunning = 1;
    return 0;
}

/* ----------------------------------------------------------------------------
 * Server task body
 *
 * run_startup is called once before the iterate loop; it starts the
 * binary protocol manager which opens the listening socket.  The loop
 * then drives the EventLoop and kicks the IWDG watchdog each iteration.
 * ---------------------------------------------------------------------------- */
void vOpcUaServerTask(void *argument)
{
    (void)argument;

    UA_StatusCode sr = UA_Server_run_startup(g_opcuaServer);
    if (sr != UA_STATUSCODE_GOOD) {
        g_opcuaRunning = 0;
    }

    while (g_opcuaRunning) {
        UA_Server_run_iterate(g_opcuaServer, true);
        osDelay(OPCUA_TASK_PERIOD_MS);
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
        /* Kick the independent watchdog so a deadlock in open62541
         * triggers a hardware reset rather than an indefinite hang. */
        extern IWDG_HandleTypeDef hiwdg;
        HAL_IWDG_Refresh(&hiwdg);
#endif
    }

    UA_Server_run_shutdown(g_opcuaServer);
    UA_Server_delete(g_opcuaServer);
    g_opcuaServer = NULL;
    /* Do NOT call osThreadTerminate(self) — just return.  The CMSIS-RTOS2
     * wrapper calls vTaskDelete(NULL) when the task function returns,
     * which is the safe FreeRTOS pattern. */
}

void OpcUaServer_Stop(void)
{
    g_opcuaRunning = 0;
}

void *OpcUaServer_GetHandle(void)
{
    return g_opcuaServer;
}
