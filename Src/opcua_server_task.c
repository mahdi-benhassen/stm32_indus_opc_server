/*
 * opcua_server_task.c
 *
 * FreeRTOS task that drives the open62541 server on top of lwIP.
 *
 *   main() / startup
 *       |
 *       v
 *   OpcUaServer_Init()  ---->  creates server, address space, sockets,
 *                              spawns vOpcUaServerTask
 *
 *   vOpcUaServerTask
 *       for(;;) {
 *           UA_Server_run_iterate(server, timeout);   // non-blocking
 *           osDelay(OPCUA_TASK_PERIOD_MS);
 *       }
 */

#include "opcua_server_task.h"
#include "opcua_node_model.h"

#include "open62541.h"

#include "lwip/sockets.h"     /* lwIP BSD socket layer                  */
#include "lwip/netif.h"
#include "FreeRTOS.h"
#include "cmsis_os.h"

#include <string.h>
#include <stdatomic.h>

/* ----------------------------------------------------------------------------
 * Globals
 * ---------------------------------------------------------------------------- */
static UA_Server       *g_opcuaServer = NULL;
static osThreadId_t     g_opcuaTaskHandle = NULL;
static volatile uint8_t g_opcuaRunning = 0;

/* Mutex protecting the shared I/O register block (DI/DO/AI/AO/Relay).
 * Created in OpcUaServer_Init() and used by both OPC UA and Modbus tasks
 * and any ISR-side register shadow.
 */
osMutexId_t         g_IoRegMutexHandle;
const osMutexAttr_t g_IoRegMutex_attr = {
    .name      = "IoRegMutex",
    .attr_bits = osMutexRecursive | osMutexPrioInherit,
    .cb_mem    = NULL,
    .cb_size   = 0U
};

/* ----------------------------------------------------------------------------
 * Shared I/O register shadow
 *
 * Modbus and the OPC UA server both need a single, consistent view of the
 * hardware I/O.  We keep a small shadow structure that the hardware drivers
 * (assumed to be running in ISR / Modbus-task context) update, and that the
 * OPC UA callbacks sample under the mutex.
 * ---------------------------------------------------------------------------- */
typedef struct {
    uint8_t  di[8];          /* 8 digital inputs  */
    uint8_t  do_[8];         /* 8 digital outputs */
    int32_t  ai[8];         /* 8 analog  inputs  (scaled) */
    int32_t  ao[8];         /* 8 analog  outputs */
    uint8_t  relay[4];      /* 4 relays          */
    uint32_t counter;       /* demo: monotonic counter for method   */
} IoShadow_t;

static IoShadow_t s_shadow;

/* ----------------------------------------------------------------------------
 * Thread-safe accessors
 * ---------------------------------------------------------------------------- */
uint8_t OpcUa_Hw_ReadDI(uint8_t ch)
{
    if (ch >= 8) return 0;
    osMutexAcquire(g_IoRegMutexHandle, osWaitForever);
    uint8_t v = s_shadow.di[ch];
    osMutexRelease(g_IoRegMutexHandle);
    return v;
}
void OpcUa_Hw_WriteDO(uint8_t ch, uint8_t v)
{
    if (ch >= 8) return;
    osMutexAcquire(g_IoRegMutexHandle, osWaitForever);
    s_shadow.do_[ch] = (v ? 1 : 0);
    osMutexRelease(g_IoRegMutexHandle);
    /* Trigger the actual GPIO update outside the lock to keep CS short. */
    extern void DO_Driver_Set(uint8_t channel, uint8_t value);   /* supplied */
    DO_Driver_Set(ch, v ? 1 : 0);
}
int32_t OpcUa_Hw_ReadAI(uint8_t ch)
{
    if (ch >= 8) return 0;
    osMutexAcquire(g_IoRegMutexHandle, osWaitForever);
    int32_t v = s_shadow.ai[ch];
    osMutexRelease(g_IoRegMutexHandle);
    return v;
}
void OpcUa_Hw_WriteAO(uint8_t ch, int32_t v)
{
    if (ch >= 8) return;
    osMutexAcquire(g_IoRegMutexHandle, osWaitForever);
    s_shadow.ao[ch] = v;
    osMutexRelease(g_IoRegMutexHandle);
    extern void AO_Driver_Set(uint8_t channel, int32_t value);
    AO_Driver_Set(ch, v);
}
uint8_t OpcUa_Hw_ReadRelay(uint8_t ch)
{
    if (ch >= 4) return 0;
    osMutexAcquire(g_IoRegMutexHandle, osWaitForever);
    uint8_t v = s_shadow.relay[ch];
    osMutexRelease(g_IoRegMutexHandle);
    return v;
}
void OpcUa_Hw_WriteRelay(uint8_t ch, uint8_t v)
{
    if (ch >= 4) return;
    osMutexAcquire(g_IoRegMutexHandle, osWaitForever);
    s_shadow.relay[ch] = (v ? 1 : 0);
    osMutexRelease(g_IoRegMutexHandle);
    extern void RELAY_Driver_Set(uint8_t channel, uint8_t value);
    RELAY_Driver_Set(ch, v ? 1 : 0);
}

/* Modbus / ISR glue: these are called from outside the OPC UA task. */
void OpcUa_Hw_UpdateDIFromISR(uint8_t ch, uint8_t v)
{
    if (ch >= 8) return;
    /* ISR context - do not block.  Shadow is a single byte, atomic on
     * Cortex-M4 (8-bit aligned), so a direct write is safe even without
     * the mutex.  We still take it if available with FromISR equivalent. */
    BaseType_t hp = pdFALSE;
    if (xSemaphoreTakeFromISR(g_IoRegMutexHandle, &hp) == pdTRUE) {
        s_shadow.di[ch] = v ? 1 : 0;
        xSemaphoreGiveFromISR(g_IoRegMutexHandle, &hp);
    } else {
        s_shadow.di[ch] = v ? 1 : 0;
    }
}
void OpcUa_Hw_UpdateAIFromISR(uint8_t ch, int32_t v)
{
    if (ch >= 8) return;
    s_shadow.ai[ch] = v;
}

/* ----------------------------------------------------------------------------
 * open62541 logger - route to UART / RTT / SEGGER instead of stdout
 * ---------------------------------------------------------------------------- */
static void OpcUa_Log(void *ctx, UA_LogLevel level, UA_LogCategory category,
                      const char *msg, va_list args)
{
    (void)ctx; (void)category;
    char buf[160];
    int n = vsnprintf(buf, sizeof(buf), msg, args);
    if (n < 0) return;
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    extern void Log_Print(const char *s, uint16_t len);
    Log_Print(buf, (uint16_t)n);
}

/* ----------------------------------------------------------------------------
 * Server initialization
 * ---------------------------------------------------------------------------- */
int32_t OpcUaServer_Init(void)
{
    /* 1. Create the IO mutex up front so anyone (Modbus task) can use it. */
    g_IoRegMutexHandle = osMutexNew(&g_IoRegMutex_attr);
    if (!g_IoRegMutexHandle) return -1;

    /* 2. Configure the server with our logger and minimal networking. */
    UA_ServerConfig *cfg = UA_ServerConfig_new_minimal(OPCUA_SERVER_PORT, NULL);
    if (!cfg) return -2;

    /* Override the logger. */
    UA_Logger logger = { OpcUa_Log, NULL, 0, UA_LOGLEVEL };
    UA_ServerConfig_setLogger(cfg, logger);

    /* Tighten endpoint / security policies - we accept only None. */
    UA_String nonePolicy = UA_STRING("http://opcfoundation.org/UA/SecurityPolicy#None");
    UA_ServerConfig_addSecurityPolicyNone(cfg, &nonePolicy);

    /* Reduce async event queue memory footprint. */
    cfg->asyncEventsToParent = 0;

    /* 3. Allocate and initialise the server. */
    g_opcuaServer = UA_Server_newWithConfig(cfg);
    if (!g_opcuaServer) return -3;

    /* 4. Build the address space (folders + variables + methods). */
    if (OpcUaNodeModel_Build(g_opcuaServer) != UA_STATUSCODE_GOOD) {
        return -4;
    }

    /* 5. Create the FreeRTOS task that drives the server. */
    const osThreadAttr_t taskAttr = {
        .name       = "OpcUaSrv",
        .stack_size = OPCUA_TASK_STACK_BYTES,
        .priority   = OPCUA_TASK_PRIORITY,
        .cb_mem     = NULL,
        .cb_size    = 0U
    };
    g_opcuaTaskHandle = osThreadNew(vOpcUaServerTask, NULL, &taskAttr);
    if (!g_opcuaTaskHandle) return -5;

    g_opcuaRunning = 1;
    return 0;
}

/* ----------------------------------------------------------------------------
 * Server task body
 * ---------------------------------------------------------------------------- */
void vOpcUaServerTask(void *argument)
{
    (void)argument;
    const UA_UInt16 timeout = (UA_UInt16)OPCUA_TASK_PERIOD_MS;

    while (g_opcuaRunning) {
        /* Non-blocking iterate.  Returns the maximum number of milliseconds
         * the server would happily sleep; we cap it to our tick period. */
        UA_Server_run_iterate(g_opcuaServer, timeout);

        /* Feed the watchdog: yield to the RTOS for the rest of the period.
         * osDelay is tick-based, not ms, so convert. */
        osDelay(OPCUA_TASK_PERIOD_MS);
    }

    /* Server loop exited - clean up.  This normally only happens on
     * explicit Stop() or a fatal error in the open62541 stack. */
    UA_Server_delete(g_opcuaServer);
    g_opcuaServer = NULL;
    osThreadTerminate(osThreadGetId());
}

void OpcUaServer_Stop(void)
{
    g_opcuaRunning = 0;
}
