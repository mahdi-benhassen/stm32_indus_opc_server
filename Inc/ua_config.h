/*
 * ua_config.h
 *
 * open62541 configuration for STM32F407 (Cortex-M4, 192KB RAM, 1MB Flash),
 * also used as the host-side ua_config.h in the CI sanity build.
 *
 * Tuned to fit in a small embedded target while still exposing browseable
 * I/O nodes and supporting subscriptions / methods.
 *
 * NOTE: This file is consumed by the open62541 amalgamated build via
 *       -DUA_CONFIG_H_FILE="ua_config.h" in the preprocessor flags.
 */

#ifndef UA_CONFIG_H_
#define UA_CONFIG_H_

/* -------------------------------------------------------------------------- */
/* Platform identification                                                     */
/* -------------------------------------------------------------------------- */
#define UA_ARCHITECTURE_POSIX
/* Force the amalgamated build to emit the "freertos" / single-thread style:    */
/* we drive the server from a single FreeRTOS task so we do NOT need its       */
/* internal pthreads / epoll wrappers.                                         */
#define UA_ARCHITECTURE_FREERTOS

/* -------------------------------------------------------------------------- */
/* Feature toggles  (smaller footprint, no discovery, no encryption)           */
/* -------------------------------------------------------------------------- */
/* Logging verbosity is configured at runtime via UA_Logger; we leave the
 * UA_LOGLEVEL macro at open62541's default to avoid clashing with their
 * internal enum tags. */
#define UA_ENABLE_METHODCALLS    1     /* We use Method nodes                */
#define UA_ENABLE_NODEMANAGEMENT 1
#define UA_ENABLE_SUBSCRIPTIONS  1     /* DataChange needed for SCADA push   */
#define UA_ENABLE_SUBSCRIPTIONS_EVENTS 0
#define UA_ENABLE_PUBSUB         0     /* No Pub/Sub over UDP - we use TCP   */
#define UA_ENABLE_DISCOVERY      0     /* No LDS, manual endpoint config     */
#define UA_ENABLE_DA             1
#define UA_ENABLE_ENCRYPTION     0     /* No PKI / certs in this build       */
#define UA_ENABLE_HISTORIZING    0     /* No history DB                      */
#define UA_ENABLE_MICRO_EMB_DEV_PROFILE 0
#define UA_ENABLE_EXPERIMENTAL_HISTORY 0

/* Access control is not required for a self-contained gateway. */
#define UA_ENABLE_ACCESSCONTROL_MANUAL 0
#ifndef UA_ENABLE_ACCESSCONTROL
#define UA_ENABLE_ACCESSCONTROL 0
#endif

/* No JSON encoding, no status codes beyond minimum set. */
#define UA_ENABLE_JSON_ENCODING 0

/* -------------------------------------------------------------------------- */
/* Memory limits  (key knobs for RAM footprint)                               */
/* -------------------------------------------------------------------------- */

/* Hard cap on simultaneous sessions. 3 is plenty for one SCADA + one
 * engineering tool. */
#define UA_MAXSESSIONCOUNT  3

/* Subscription limits. SCADA polls + 1 monitored item per variable is fine. */
#define UA_MAXSUBSCRIPTIONCOUNT  4
#define UA_MAXMONITOREDITEMCOUNT 64

/* Chunking - reduce to lower per-message heap. */
#define UA_MAXMESSAGEBYTESTOBUFFERED  4096
#define UA_MAXMESSAGEBODYSIZE         4096
#define UA_MAXMESSAGECHUNKS           4
#define UA_MAXCHUNKPATCHSIZE          1024

/* Server / SecureChannel limits. */
#define UA_MAXSECURECHANNELID         40
#define UA_MAXSECURECHANNELS          4
#define UA_SESSION_LOCAL_SESSIONS     4
#define UA_SESSION_CONNECTION_TIMEOUT 300000   /* ms */

/* Number of nodes in our static address space - keep it small and known. */
#ifndef UA_NODESTORE_MAXSIZE
#define UA_NODESTORE_MAXSIZE 256
#endif

/* String / ByteString limits - DI/DO/AO names are short, this is enough. */
#define UA_MAXSTRINGLENGTH 128
#define UA_MAXBYTESTRINGLENGTH 128

/* -------------------------------------------------------------------------- */
/* Custom memory allocator - bridge to FreeRTOS heap_4 or heap_5.              */
/* pvPortMalloc / vPortFree are thread-safe (interrupt-disabling) and are     */
/* safe to call from the OPC UA task context.                                  */
/* -------------------------------------------------------------------------- */
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
  #include "FreeRTOS.h"
  #define UA_free(ptr)        vPortFree(ptr)
  #define UA_malloc(size)     pvPortMalloc(size)
  #define UA_calloc(num,sz)   ({ void *_p = pvPortMalloc((num)*(sz));  \
                                  if(_p) memset(_p,0,(num)*(sz)); _p; })
  #define UA_realloc(ptr,sz)  ({ void *_p = pvPortMalloc(sz);          \
                                  if(_p && (ptr)) { memcpy(_p,(ptr),(sz)); vPortFree(ptr); } _p; })
#else
  /* Host / CI build - fall back to the C library allocator. */
  #include <stdlib.h>
  #include <string.h>
  #define UA_free(ptr)        free(ptr)
  #define UA_malloc(size)     malloc(size)
  #define UA_calloc(num,sz)   calloc((num),(sz))
  #define UA_realloc(ptr,sz)  realloc((ptr),(sz))
#endif

/* -------------------------------------------------------------------------- */
/* Stack / threading                                                          */
/* -------------------------------------------------------------------------- */
/* open62541's amalgamated header still emits UA_LOCK_INIT / UA_LOCK / UA_UNLOCK
 * macros that expand to pthread_* calls, even if UA_ENABLE_PTHREADS is 0.  We
 * do NOT want pthread on the embedded target, so we stub them out completely.
 * Our application code uses its own CMSIS-RTOS2 / FreeRTOS mutex.
 */
#define UA_ENABLE_PTHREADS 0
#define UA_LOCK_INIT(lock)            do { (void)(lock); } while (0)
#define UA_LOCK_DESTROY(lock)         do { (void)(lock); } while (0)
#define UA_LOCK(lock)                 do { (void)(lock); } while (0)
#define UA_UNLOCK(lock)               do { (void)(lock); } while (0)

/* -------------------------------------------------------------------------- */
/* Network - we use the lwIP Netconn / BSD socket API from the OPC UA task.   */
/* open62541 needs POSIX sockets; lwIP provides them via the "sys_arch"       */
/* compatibility layer (LWIP_COMPAT_SOCKETS).  We keep open62541's socket     */
/* layer enabled.                                                              */
/* -------------------------------------------------------------------------- */
#define UA_ENABLE_SOCKET_NETLAYER 1
#define UA_ENABLE_SOCKET_EVENTLOOP 0   /* we drive iterate() ourselves */

#endif /* UA_CONFIG_H_ */
