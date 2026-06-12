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
 *       for(;;) {
 *           UA_Server_run_iterate(server, true);  // wait for messages
 *           osDelay(OPCUA_TASK_PERIOD_MS);
 *       }
 */

#include "opcua_server_task.h"
#include "opcua_node_model.h"

#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
  #include "lwip/sockets.h"
  #include "lwip/netif.h"
  #include "FreeRTOS.h"
  #include "cmsis_os.h"
  #include <stdio.h>
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
OpcUaMutexId g_IoRegMutexHandle;
#else
OpcUaMutexId g_IoRegMutexHandle = (OpcUaMutexId)0;
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

#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
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
 * ---------------------------------------------------------------------------- */
static void OpcUa_Log(void *ctx, UA_LogLevel level, UA_LogCategory category,
                      const char *msg, va_list args)
{
    (void)ctx; (void)level; (void)category; (void)msg; (void)args;
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
    char buf[160];
    int n = vsnprintf(buf, sizeof(buf), msg, args);
    if (n < 0) return;
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    extern void Log_Print(const char *s, uint16_t len);
    Log_Print(buf, (uint16_t)n);
#endif
}

static UA_Logger g_opcuaLogger = {
    .log     = OpcUa_Log,
    .context = NULL,
    .clear   = NULL
};

/* ----------------------------------------------------------------------------
 * Server initialization
 * ---------------------------------------------------------------------------- */
int32_t OpcUaServer_Init(void)
{
    /* 1. Create the IO mutex up front. */
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
    g_IoRegMutexHandle = osMutexNew(&g_IoRegMutex_attr);
    if (!g_IoRegMutexHandle) return -1;
#endif

    /* 2. Build a server with a default config and the right logger. */
    g_opcuaServer = UA_Server_new();
    if (!g_opcuaServer) return -2;

    UA_ServerConfig *cfg = UA_Server_getConfig(g_opcuaServer);
    UA_ServerConfig_setMinimal(cfg, OPCUA_SERVER_PORT, NULL);
    UA_ServerConfig_addSecurityPolicyNone(cfg, NULL);
    cfg->logging = &g_opcuaLogger;

    /* 3. Build the address space. */
    if (OpcUaNodeModel_Build(g_opcuaServer) != UA_STATUSCODE_GOOD) return -3;

    /* 4. Spawn the FreeRTOS task that drives the server. */
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
        if (!g_opcuaTaskHandle) return -4;
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
 * ---------------------------------------------------------------------------- */
void vOpcUaServerTask(void *argument)
{
    (void)argument;
    while (g_opcuaRunning) {
        UA_Server_run_iterate(g_opcuaServer, true);
        osDelay(OPCUA_TASK_PERIOD_MS);
    }
    UA_Server_delete(g_opcuaServer);
    g_opcuaServer = NULL;
    osThreadTerminate(osThreadGetId());
}

void OpcUaServer_Stop(void)
{
    g_opcuaRunning = 0;
}
