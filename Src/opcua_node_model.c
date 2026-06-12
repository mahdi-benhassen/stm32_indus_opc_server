/*
 * opcua_node_model.c
 *
 * Builds the OPC UA address space for the gateway and wires every variable
 * node to the real hardware getters/setters via UA_ValueCallback.  All
 * hardware access goes through OpcUa_Hw_* helpers in opcua_server_task.c
 * which take the IO mutex.
 */

#include "opcua_node_model.h"
#include "opcua_server_task.h"   /* OpcUa_Hw_* prototypes               */

#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
  #include "open62541.h"
  #include "FreeRTOS.h"
#endif

#include <stdio.h>   /* snprintf */
#include <string.h>
#include <stdint.h>

/* ----------------------------------------------------------------------------
 * Forward declarations of the ValueCallback read/write functions.
 * We pass them to UA_Server_setVariableNode_valueSource so the server can
 * call them every time a client reads / writes the node.  This avoids
 * shadowing the data in the OPC UA server's node store.
 * ---------------------------------------------------------------------------- */
static void cb_ReadBoolean (UA_Server *s, const UA_NodeId *n,
                            UA_DataValue *dv, void *range);
static void cb_WriteBoolean(UA_Server *s, const UA_NodeId *n,
                            const UA_DataValue *dv, void *range);
static void cb_ReadInt32   (UA_Server *s, const UA_NodeId *n,
                            UA_DataValue *dv, void *range);
static void cb_WriteInt32  (UA_Server *s, const UA_NodeId *n,
                            const UA_DataValue *dv, void *range);

/* ----------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------------- */
static UA_NodeId
addObjectFolder(UA_Server *s, UA_NodeId parent, const char *browseName)
{
    UA_ObjectAttributes oa = UA_ObjectAttributes_default;
    oa.displayName = UA_LOCALIZED_TEXT("en-US", (char *)browseName);
    oa.description = UA_LOCALIZED_TEXT("en-US", (char *)browseName);
    oa.writeMask   = 0;
    UA_NodeId id;
    UA_StatusCode r = UA_Server_addObjectNode(
        s, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIED_NAME(1, (char *)browseName),
        UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
        oa, NULL, &id);
    (void)r;
    return id;
}

static void
addBooleanVar(UA_Server *s, UA_NodeId parent, const char *name,
              UA_NodeId *outId,
              void (*onRead)(UA_Server*, const UA_NodeId*,
                             UA_DataValue*, void*),
              void (*onWrite)(UA_Server*, const UA_NodeId*,
                              const UA_DataValue*, void*),
              UA_Byte accessLevel)
{
    UA_VariableAttributes va = UA_VariableAttributes_default;
    va.displayName = UA_LOCALIZED_TEXT("en-US", (char *)name);
    va.description = UA_LOCALIZED_TEXT("en-US", (char *)name);
    va.dataType    = UA_NODEID_NUMERIC(0, UA_NS0ID_BOOLEAN);
    va.valueRank   = UA_VALUERANK_SCALAR;
    UA_Boolean init = false;
    UA_Variant_setScalarCopy(&va.value, &init, &UA_TYPES[UA_TYPES_BOOLEAN]);
    va.accessLevel = accessLevel;

    UA_StatusCode r = UA_Server_addVariableNode(
        s, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIED_NAME(1, (char *)name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        va, NULL, outId);
    (void)r;

    UA_ValueSource vs = { 0 };
    vs.read  = onRead;
    vs.write = onWrite;
    UA_Server_setVariableNode_valueSource(s, *outId, vs);
}

static void
addInt32Var(UA_Server *s, UA_NodeId parent, const char *name,
            UA_NodeId *outId,
            void (*onRead)(UA_Server*, const UA_NodeId*,
                           UA_DataValue*, void*),
            void (*onWrite)(UA_Server*, const UA_NodeId*,
                            const UA_DataValue*, void*),
            UA_Byte accessLevel)
{
    UA_VariableAttributes va = UA_VariableAttributes_default;
    va.displayName = UA_LOCALIZED_TEXT("en-US", (char *)name);
    va.description = UA_LOCALIZED_TEXT("en-US", (char *)name);
    va.dataType    = UA_NODEID_NUMERIC(0, UA_NS0ID_INT32);
    va.valueRank   = UA_VALUERANK_SCALAR;
    UA_Int32 init  = 0;
    UA_Variant_setScalarCopy(&va.value, &init, &UA_TYPES[UA_TYPES_INT32]);
    va.accessLevel = accessLevel;

    UA_StatusCode r = UA_Server_addVariableNode(
        s, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIED_NAME(1, (char *)name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        va, NULL, outId);
    (void)r;

    UA_ValueSource vs = { 0 };
    vs.read  = onRead;
    vs.write = onWrite;
    UA_Server_setVariableNode_valueSource(s, *outId, vs);
}

/* ----------------------------------------------------------------------------
 * Read / Write callbacks for hardware-bound variables.
 *
 * We dispatch on the node's BrowseName, but the node-id is already known
 * to the caller (we assigned them sequentially).  In production builds
 * store the index in a UA_NodeId numeric ID and decode via the id number.
 * For clarity here we use UA_Server_readNodeIdValue + name lookup is too
 * heavy, so we instead encode the channel index in the node-id:
 *       DI_n  -> ns=1, id=0x1000+n
 *       DO_n  -> ns=1, id=0x2000+n
 *       AI_n  -> ns=1, id=0x3000+n
 *       AO_n  -> ns=1, id=0x4000+n
 *       RLY_n -> ns=1, id=0x5000+n
 * ---------------------------------------------------------------------------- */
#define NS_IO 1
#define ID_DI_BASE   0x1000
#define ID_DO_BASE   0x2000
#define ID_AI_BASE   0x3000
#define ID_AO_BASE   0x4000
#define ID_RELAY_BASE 0x5000

static void
cb_ReadBoolean(UA_Server *s, const UA_NodeId *n,
               UA_DataValue *dv, void *range)
{
    (void)range;
    UA_Boolean v = false;
    uint16_t id = n->identifier.numeric - ID_DI_BASE;
    if (n->namespaceIndex == NS_IO) {
        if (n->identifier.numeric >= ID_DI_BASE &&
            n->identifier.numeric <  ID_DI_BASE + IO_DI_COUNT) {
            v = OpcUa_Hw_ReadDI((uint8_t)id) ? true : false;
        } else if (n->identifier.numeric >= ID_RELAY_BASE &&
                   n->identifier.numeric <  ID_RELAY_BASE + IO_RELAY_COUNT) {
            v = OpcUa_Hw_ReadRelay((uint8_t)id) ? true : false;
        }
    }
    UA_Variant_setScalarCopy(&dv->value, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
    dv->hasValue = true;
    dv->sourceTimestamp = UA_DateTime_now();
    dv->sourcePicoseconds = 0;
    dv->status = UA_STATUSCODE_GOOD;
}

static void
cb_WriteBoolean(UA_Server *s, const UA_NodeId *n,
                const UA_DataValue *dv, void *range)
{
    (void)range;
    if (!dv->hasValue || !UA_Variant_isScalar(&dv->value)) return;
    UA_Boolean v = *(UA_Boolean *)dv->value.data;
    if (n->namespaceIndex != NS_IO) return;
    uint16_t id = n->identifier.numeric - ID_DO_BASE;
    if (n->identifier.numeric >= ID_DO_BASE &&
        n->identifier.numeric <  ID_DO_BASE + IO_DO_COUNT) {
        OpcUa_Hw_WriteDO((uint8_t)id, v ? 1 : 0);
    } else if (n->identifier.numeric >= ID_RELAY_BASE &&
               n->identifier.numeric <  ID_RELAY_BASE + IO_RELAY_COUNT) {
        OpcUa_Hw_WriteRelay((uint8_t)id, v ? 1 : 0);
    }
}

static void
cb_ReadInt32(UA_Server *s, const UA_NodeId *n,
             UA_DataValue *dv, void *range)
{
    (void)range;
    UA_Int32 v = 0;
    if (n->namespaceIndex == NS_IO &&
        n->identifier.numeric >= ID_AI_BASE &&
        n->identifier.numeric <  ID_AI_BASE + IO_AI_COUNT) {
        uint16_t id = n->identifier.numeric - ID_AI_BASE;
        v = OpcUa_Hw_ReadAI((uint8_t)id);
    }
    UA_Variant_setScalarCopy(&dv->value, &v, &UA_TYPES[UA_TYPES_INT32]);
    dv->hasValue = true;
    dv->sourceTimestamp = UA_DateTime_now();
    dv->sourcePicoseconds = 0;
    dv->status = UA_STATUSCODE_GOOD;
}

static void
cb_WriteInt32(UA_Server *s, const UA_NodeId *n,
              const UA_DataValue *dv, void *range)
{
    (void)range;
    if (!dv->hasValue || !UA_Variant_isScalar(&dv->value)) return;
    UA_Int32 v = *(UA_Int32 *)dv->value.data;
    if (n->namespaceIndex != NS_IO) return;
    if (n->identifier.numeric < ID_AO_BASE ||
        n->identifier.numeric >= ID_AO_BASE + IO_AO_COUNT) return;
    uint16_t id = n->identifier.numeric - ID_AO_BASE;
    OpcUa_Hw_WriteAO((uint8_t)id, v);
}

/* ----------------------------------------------------------------------------
 * Method implementations
 * ---------------------------------------------------------------------------- */
static UA_StatusCode
methodResetCounter(UA_Server *s,
                   const UA_NodeId *sessionId, void *sessionContext,
                   const UA_NodeId *methodId, void *methodContext,
                   size_t inputSize, const UA_Variant *input,
                   size_t outputSize, UA_Variant *output)
{
    (void)s; (void)sessionId; (void)sessionContext;
    (void)methodId; (void)methodContext;
    (void)inputSize; (void)input; (void)outputSize; (void)output;
    extern void Counter_Reset(void);
    Counter_Reset();
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
methodEmergencyStop(UA_Server *s,
                    const UA_NodeId *sessionId, void *sessionContext,
                    const UA_NodeId *methodId, void *methodContext,
                    size_t inputSize, const UA_Variant *input,
                    size_t outputSize, UA_Variant *output)
{
    (void)s; (void)sessionId; (void)sessionContext;
    (void)methodId; (void)methodContext;
    (void)inputSize; (void)input; (void)outputSize; (void)output;
    /* Force all DOs and relays off - this is a safety action. */
    for (uint8_t i = 0; i < IO_DO_COUNT;   i++) OpcUa_Hw_WriteDO(i, 0);
    for (uint8_t i = 0; i < IO_RELAY_COUNT; i++) OpcUa_Hw_WriteRelay(i, 0);
    return UA_STATUSCODE_GOOD;
}

/* ----------------------------------------------------------------------------
 * Public entry point
 * ---------------------------------------------------------------------------- */
UA_StatusCode OpcUaNodeModel_Build(UA_Server *server)
{
    /* ---- Top-level folder under Objects/ --------------------------------- */
    UA_NodeId industrialIO = addObjectFolder(server,
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), "Industrial_IO");

    /* ---- Sub-folders ----------------------------------------------------- */
    UA_NodeId diFolder = addObjectFolder(server, industrialIO, "DigitalInputs");
    UA_NodeId doFolder = addObjectFolder(server, industrialIO, "DigitalOutputs");
    UA_NodeId aiFolder = addObjectFolder(server, industrialIO, "AnalogInputs");
    UA_NodeId aoFolder = addObjectFolder(server, industrialIO, "AnalogOutputs");
    UA_NodeId rlFolder = addObjectFolder(server, industrialIO, "Relays");
    UA_NodeId mtFolder = addObjectFolder(server, industrialIO, "Methods");

    /* ---- DI: 8 read-only booleans ---------------------------------------- */
    for (uint8_t i = 0; i < IO_DI_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "DI_%02u", i);
        UA_NodeId id = UA_NODEID_NUMERIC(NS_IO, (UA_UInt32)(ID_DI_BASE + i));
        addBooleanVar(server, diFolder, name, &id,
                      cb_ReadBoolean, NULL,
                      UA_ACCESSLEVELMASK_READ);
    }

    /* ---- DO: 8 read/write booleans --------------------------------------- */
    for (uint8_t i = 0; i < IO_DO_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "DO_%02u", i);
        UA_NodeId id = UA_NODEID_NUMERIC(NS_IO, (UA_UInt32)(ID_DO_BASE + i));
        addBooleanVar(server, doFolder, name, &id,
                      cb_ReadBoolean, cb_WriteBoolean,
                      UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE);
    }

    /* ---- AI: 8 read-only Int32 ------------------------------------------- */
    for (uint8_t i = 0; i < IO_AI_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "AI_%02u", i);
        UA_NodeId id = UA_NODEID_NUMERIC(NS_IO, (UA_UInt32)(ID_AI_BASE + i));
        addInt32Var(server, aiFolder, name, &id,
                    cb_ReadInt32, NULL,
                    UA_ACCESSLEVELMASK_READ);
    }

    /* ---- AO: 8 read/write Int32 ----------------------------------------- */
    for (uint8_t i = 0; i < IO_AO_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "AO_%02u", i);
        UA_NodeId id = UA_NODEID_NUMERIC(NS_IO, (UA_UInt32)(ID_AO_BASE + i));
        addInt32Var(server, aoFolder, name, &id,
                    cb_ReadInt32, cb_WriteInt32,
                    UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE);
    }

    /* ---- Relays: 4 read/write booleans --------------------------------- */
    for (uint8_t i = 0; i < IO_RELAY_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "RLY_%02u", i);
        UA_NodeId id = UA_NODEID_NUMERIC(NS_IO, (UA_UInt32)(ID_RELAY_BASE + i));
        addBooleanVar(server, rlFolder, name, &id,
                      cb_ReadBoolean, cb_WriteBoolean,
                      UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE);
    }

    /* ---- Methods -------------------------------------------------------- */
    UA_Argument noArg;
    UA_Argument_init(&noArg);
    noArg.dataType = UA_NODEID_NUMERIC(0, UA_NS0ID_BOOLEAN);
    noArg.name     = UA_LOCALIZED_TEXT("en-US", "ignored");
    noArg.valueRank = UA_VALUERANK_SCALAR;

    UA_NodeId mReset;
    UA_Server_addMethodNode(
        server, UA_NODEID_NULL, mtFolder,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIED_NAME(1, "ResetCounter"),
        UA_LOCALIZED_TEXT("en-US", "ResetCounter"),
        UA_LOCALIZED_TEXT("en-US", "Resets the gateway's internal counter"),
        methodResetCounter, 0, NULL, 0, NULL,
        NULL, &mReset);

    UA_NodeId mEStop;
    UA_Server_addMethodNode(
        server, UA_NODEID_NULL, mtFolder,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIED_NAME(1, "EmergencyStop"),
        UA_LOCALIZED_TEXT("en-US", "EmergencyStop"),
        UA_LOCALIZED_TEXT("en-US", "Force all DOs and Relays to OFF"),
        methodEmergencyStop, 0, NULL, 0, NULL,
        NULL, &mEStop);

    return UA_STATUSCODE_GOOD;
}
