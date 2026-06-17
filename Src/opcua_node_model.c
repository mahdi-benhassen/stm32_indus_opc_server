/*
 * opcua_node_model.c
 *
 * OPC UA Information Model for the Industry 4.0 gateway.
 *
 * Target API: open62541 v1.5.4 (amalgamated single-file build).
 *
 *   Objects/
 *     └─ Industrial_IO  (Object)
 *          ├─ DigitalInputs   (Folder)
 *          │    ├─ DI_00 .. DI_07  (Boolean, RO)
 *          ├─ DigitalOutputs  (Folder)
 *          │    ├─ DO_00 .. DO_07  (Boolean, RW)
 *          ├─ AnalogInputs    (Folder)
 *          │    ├─ AI_00 .. AI_07  (Int32,   RO)
 *          ├─ AnalogOutputs   (Folder)
 *          │    ├─ AO_00 .. AO_07  (Int32,   RW)
 *          ├─ Relays          (Folder)
 *          │    ├─ RLY_00 .. RLY_03 (Boolean, RW)
 *          └─ Methods
 *               ├─ ResetCounter  (Method)
 *               └─ EmergencyStop (Method)
 *
 * Variable nodes are bound to the underlying hardware via
 * UA_CallbackValueSource: every read/write is a direct call into the
 * OpcUa_Hw_* helpers in opcua_server_task.c, which take the IO mutex
 * before touching the hardware register shadow.
 */

#include "opcua_node_model.h"
#include "opcua_server_task.h"

#include "open62541.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ----------------------------------------------------------------------------
 * NodeId scheme
 *
 *  DI_n   -> ns=1, i=0x1000+n
 *  DO_n   -> ns=1, i=0x2000+n
 *  AI_n   -> ns=1, i=0x3000+n
 *  AO_n   -> ns=1, i=0x4000+n
 *  RLY_n  -> ns=1, i=0x5000+n
 * ---------------------------------------------------------------------------- */
#define ID_DI_BASE    0x1000
#define ID_DO_BASE    0x2000
#define ID_AI_BASE    0x3000
#define ID_AO_BASE    0x4000
#define ID_RELAY_BASE 0x5000

/* Resolved at runtime by OpcUaNodeModel_Build. */
static UA_UInt16 s_nsIdx = 1;

static UA_NodeId
nodeId(UA_UInt32 id)
{
    return UA_NODEID_NUMERIC(s_nsIdx, id);
}

static UA_QualifiedName
qname(const char *name)
{
    return UA_QUALIFIEDNAME(s_nsIdx, (char *)name);
}

static UA_LocalizedText
lt(const char *text)
{
    return UA_LOCALIZEDTEXT("en-US", (char *)text);
}

/* ----------------------------------------------------------------------------
 * Helpers
 *
 * Every UA_Server_add*Node return code is checked.  If any node fails to
 * create (e.g. out of memory), OpcUaNodeModel_Build returns the error
 * status and the caller deletes the server, cleaning up any partially
 * built nodes.
 * ---------------------------------------------------------------------------- */
static UA_StatusCode
addObjectFolder(UA_Server *s, UA_NodeId parent, const char *browseName,
                UA_NodeId *outId)
{
    UA_ObjectAttributes oa = UA_ObjectAttributes_default;
    oa.displayName = lt(browseName);
    oa.description = lt(browseName);
    return UA_Server_addObjectNode(s,
                                   UA_NODEID_NULL,
                                   parent,
                                   UA_NS0ID(ORGANIZES),
                                   qname(browseName),
                                   UA_NS0ID(FOLDERTYPE),
                                   oa, NULL, outId);
}

/* ----------------------------------------------------------------------------
 * Value source callbacks
 *
 * The read function copies a value from the hardware shadow into the
 * UA_DataValue supplied by the server engine.  The write function
 * decodes the client-supplied variant and pushes it into the hardware
 * shadow + driver.
 * ---------------------------------------------------------------------------- */
static UA_StatusCode
cb_ReadBoolean(UA_Server *server,
               const UA_NodeId *sessionId, void *sessionContext,
               const UA_NodeId *nodeId, void *nodeContext,
               UA_Boolean includeSourceTimestamp,
               const UA_NumericRange *range,
               UA_DataValue *out)
{
    (void)server; (void)sessionId; (void)sessionContext;
    (void)nodeContext; (void)includeSourceTimestamp; (void)range;

    UA_Boolean v = false;
    UA_Boolean found = false;
    if (nodeId->namespaceIndex == s_nsIdx) {
        UA_UInt32 id = nodeId->identifier.numeric;
        if (id >= ID_DI_BASE && id < ID_DI_BASE + IO_DI_COUNT) {
            v = OpcUa_Hw_ReadDI((uint8_t)(id - ID_DI_BASE)) ? true : false;
            found = true;
        } else if (id >= ID_DO_BASE && id < ID_DO_BASE + IO_DO_COUNT) {
            v = OpcUa_Hw_ReadDO((uint8_t)(id - ID_DO_BASE)) ? true : false;
            found = true;
        } else if (id >= ID_RELAY_BASE && id < ID_RELAY_BASE + IO_RELAY_COUNT) {
            v = OpcUa_Hw_ReadRelay((uint8_t)(id - ID_RELAY_BASE)) ? true : false;
            found = true;
        }
    }

    if (!found) {
        out->hasValue = false;
        out->status = UA_STATUSCODE_BADNODEIDUNKNOWN;
        return UA_STATUSCODE_BADNODEIDUNKNOWN;
    }
    UA_Variant_setScalarCopy(&out->value, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
    out->hasValue = true;
    out->sourceTimestamp = UA_DateTime_now();
    out->status = UA_STATUSCODE_GOOD;
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
cb_WriteBoolean(UA_Server *server,
                const UA_NodeId *sessionId, void *sessionContext,
                const UA_NodeId *nodeId, void *nodeContext,
                const UA_NumericRange *range,
                const UA_DataValue *value)
{
    (void)server; (void)sessionId; (void)sessionContext;
    (void)nodeContext; (void)range;

    if (!value->hasValue || !UA_Variant_hasScalarType(&value->value,
                                                     &UA_TYPES[UA_TYPES_BOOLEAN]))
        return UA_STATUSCODE_BADTYPEMISMATCH;

    UA_Boolean v = *(UA_Boolean *)value->value.data;
    if (nodeId->namespaceIndex != s_nsIdx) return UA_STATUSCODE_BADNODEIDUNKNOWN;
    UA_UInt32 id = nodeId->identifier.numeric;
    if (id >= ID_DO_BASE && id < ID_DO_BASE + IO_DO_COUNT) {
        OpcUa_Hw_WriteDO((uint8_t)(id - ID_DO_BASE), v ? 1 : 0);
    } else if (id >= ID_RELAY_BASE && id < ID_RELAY_BASE + IO_RELAY_COUNT) {
        OpcUa_Hw_WriteRelay((uint8_t)(id - ID_RELAY_BASE), v ? 1 : 0);
    } else {
        return UA_STATUSCODE_BADNODEIDUNKNOWN;
    }
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
cb_ReadInt32(UA_Server *server,
             const UA_NodeId *sessionId, void *sessionContext,
             const UA_NodeId *nodeId, void *nodeContext,
             UA_Boolean includeSourceTimestamp,
             const UA_NumericRange *range,
             UA_DataValue *out)
{
    (void)server; (void)sessionId; (void)sessionContext;
    (void)nodeContext; (void)includeSourceTimestamp; (void)range;

    UA_Int32 v = 0;
    UA_Boolean found = false;
    if (nodeId->namespaceIndex == s_nsIdx) {
        UA_UInt32 id = nodeId->identifier.numeric;
        if (id >= ID_AI_BASE && id < ID_AI_BASE + IO_AI_COUNT) {
            v = OpcUa_Hw_ReadAI((uint8_t)(id - ID_AI_BASE));
            found = true;
        } else if (id >= ID_AO_BASE && id < ID_AO_BASE + IO_AO_COUNT) {
            v = OpcUa_Hw_ReadAO((uint8_t)(id - ID_AO_BASE));
            found = true;
        }
    }

    if (!found) {
        out->hasValue = false;
        out->status = UA_STATUSCODE_BADNODEIDUNKNOWN;
        return UA_STATUSCODE_BADNODEIDUNKNOWN;
    }
    UA_Variant_setScalarCopy(&out->value, &v, &UA_TYPES[UA_TYPES_INT32]);
    out->hasValue = true;
    out->sourceTimestamp = UA_DateTime_now();
    out->status = UA_STATUSCODE_GOOD;
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
cb_WriteInt32(UA_Server *server,
              const UA_NodeId *sessionId, void *sessionContext,
              const UA_NodeId *nodeId, void *nodeContext,
              const UA_NumericRange *range,
              const UA_DataValue *value)
{
    (void)server; (void)sessionId; (void)sessionContext;
    (void)nodeContext; (void)range;

    if (!value->hasValue ||
        !UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_INT32]))
        return UA_STATUSCODE_BADTYPEMISMATCH;

    UA_Int32 v = *(UA_Int32 *)value->value.data;
    if (nodeId->namespaceIndex != s_nsIdx) return UA_STATUSCODE_BADNODEIDUNKNOWN;
    UA_UInt32 id = nodeId->identifier.numeric;
    if (id < ID_AO_BASE || id >= ID_AO_BASE + IO_AO_COUNT)
        return UA_STATUSCODE_BADNODEIDUNKNOWN;

    OpcUa_Hw_WriteAO((uint8_t)(id - ID_AO_BASE), v);
    return UA_STATUSCODE_GOOD;
}

/* ----------------------------------------------------------------------------
 * Variable-node creation helper
 * ---------------------------------------------------------------------------- */
static UA_StatusCode
addCallbackVar(UA_Server *s, UA_NodeId parent, const char *name,
               UA_NodeId requestedId, UA_NodeId typeId,
               UA_Byte accessLevel,
               UA_StatusCode (*readFn)(UA_Server*, const UA_NodeId*, void*,
                                       const UA_NodeId*, void*,
                                       UA_Boolean, const UA_NumericRange*,
                                       UA_DataValue*),
               UA_StatusCode (*writeFn)(UA_Server*, const UA_NodeId*, void*,
                                        const UA_NodeId*, void*,
                                        const UA_NumericRange*,
                                        const UA_DataValue*))
{
    UA_VariableAttributes va = UA_VariableAttributes_default;
    va.displayName = lt((char *)name);
    va.description = lt((char *)name);
    va.dataType    = typeId;
    va.valueRank   = UA_VALUERANK_SCALAR;
    va.accessLevel = accessLevel;

    /* Initialise the value so the server has a sane scalar to return
     * before the first callback fires. */
    UA_NodeId boolId = UA_NS0ID(BOOLEAN);
    if (UA_NodeId_equal(&typeId, &boolId)) {
        UA_Boolean z = false;
        UA_Variant_setScalarCopy(&va.value, &z, &UA_TYPES[UA_TYPES_BOOLEAN]);
    } else {
        UA_Int32 z = 0;
        UA_Variant_setScalarCopy(&va.value, &z, &UA_TYPES[UA_TYPES_INT32]);
    }

    UA_CallbackValueSource cvs;
    cvs.read  = readFn;
    cvs.write = writeFn;

    UA_NodeId outId;
    UA_StatusCode r = UA_Server_addCallbackValueSourceVariableNode(
        s, requestedId, parent,
        UA_NS0ID(HASCOMPONENT), qname(name), UA_NS0ID(BASEDATAVARIABLETYPE),
        va, cvs, NULL, &outId);

    /* UA_Server_addCallbackValueSourceVariableNode deep-copies va (including
     * va.value) so we must clear our local copy to avoid leaking the
     * Variant_setScalarCopy allocation. */
    UA_Variant_clear(&va.value);
    return r;
}

/* ----------------------------------------------------------------------------
 * Method callbacks
 * ---------------------------------------------------------------------------- */
static UA_StatusCode
methodResetCounter(UA_Server *s,
                   const UA_NodeId *sessionId, void *sessionContext,
                   const UA_NodeId *methodId, void *methodContext,
                   const UA_NodeId *objectId, void *objectContext,
                   size_t inputSize, const UA_Variant *input,
                   size_t outputSize, UA_Variant *output)
{
    (void)s; (void)sessionId; (void)sessionContext; (void)methodId;
    (void)methodContext; (void)objectId; (void)objectContext;
    (void)inputSize; (void)input; (void)outputSize; (void)output;
#if defined(OPCUA_EMBEDDED_TARGET) && (OPCUA_EMBEDDED_TARGET == 1)
    extern void Counter_Reset(void);
    Counter_Reset();
#endif
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
methodEmergencyStop(UA_Server *s,
                    const UA_NodeId *sessionId, void *sessionContext,
                    const UA_NodeId *methodId, void *methodContext,
                    const UA_NodeId *objectId, void *objectContext,
                    size_t inputSize, const UA_Variant *input,
                    size_t outputSize, UA_Variant *output)
{
    (void)s; (void)sessionId; (void)sessionContext; (void)methodId;
    (void)methodContext; (void)objectId; (void)objectContext;
    (void)inputSize; (void)input; (void)outputSize; (void)output;
    /* Atomic: hold the IO mutex across all DO + Relay writes so a
     * concurrent SCADA write cannot leave an output ON. */
    OpcUa_Hw_EmergencyStopAll();
    return UA_STATUSCODE_GOOD;
}

/* ----------------------------------------------------------------------------
 * Public entry point
 * ---------------------------------------------------------------------------- */
UA_StatusCode OpcUaNodeModel_Build(UA_Server *server)
{
    UA_StatusCode r;

    /* Register our namespace and store the actual index (the server
     * may already have ns 0 and ns 1 allocated, so the returned index
     * might be 2 or higher). */
    s_nsIdx = UA_Server_addNamespace(server, IO_NAMESPACE_URI);

    /* Top-level folder under Objects/. */
    UA_NodeId industrialIO;
    r = addObjectFolder(server, UA_NS0ID(OBJECTSFOLDER), "Industrial_IO",
                        &industrialIO);
    if (r != UA_STATUSCODE_GOOD) return r;

    UA_NodeId diFolder, doFolder, aiFolder, aoFolder, rlFolder, mtFolder;
    if ((r = addObjectFolder(server, industrialIO, "DigitalInputs",  &diFolder)) != UA_STATUSCODE_GOOD) return r;
    if ((r = addObjectFolder(server, industrialIO, "DigitalOutputs", &doFolder)) != UA_STATUSCODE_GOOD) return r;
    if ((r = addObjectFolder(server, industrialIO, "AnalogInputs",   &aiFolder)) != UA_STATUSCODE_GOOD) return r;
    if ((r = addObjectFolder(server, industrialIO, "AnalogOutputs",  &aoFolder)) != UA_STATUSCODE_GOOD) return r;
    if ((r = addObjectFolder(server, industrialIO, "Relays",         &rlFolder)) != UA_STATUSCODE_GOOD) return r;
    if ((r = addObjectFolder(server, industrialIO, "Methods",        &mtFolder)) != UA_STATUSCODE_GOOD) return r;

    /* DI: read-only booleans. */
    for (uint8_t i = 0; i < IO_DI_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "DI_%02u", (unsigned)i);
        r = addCallbackVar(server, diFolder, name, nodeId(ID_DI_BASE + i),
                           UA_NS0ID(BOOLEAN),
                           UA_ACCESSLEVELMASK_READ,
                           cb_ReadBoolean, NULL);
        if (r != UA_STATUSCODE_GOOD) return r;
    }

    /* DO: read/write booleans. */
    for (uint8_t i = 0; i < IO_DO_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "DO_%02u", (unsigned)i);
        r = addCallbackVar(server, doFolder, name, nodeId(ID_DO_BASE + i),
                           UA_NS0ID(BOOLEAN),
                           UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE,
                           cb_ReadBoolean, cb_WriteBoolean);
        if (r != UA_STATUSCODE_GOOD) return r;
    }

    /* AI: read-only Int32. */
    for (uint8_t i = 0; i < IO_AI_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "AI_%02u", (unsigned)i);
        r = addCallbackVar(server, aiFolder, name, nodeId(ID_AI_BASE + i),
                           UA_NS0ID(INT32),
                           UA_ACCESSLEVELMASK_READ,
                           cb_ReadInt32, NULL);
        if (r != UA_STATUSCODE_GOOD) return r;
    }

    /* AO: read/write Int32. */
    for (uint8_t i = 0; i < IO_AO_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "AO_%02u", (unsigned)i);
        r = addCallbackVar(server, aoFolder, name, nodeId(ID_AO_BASE + i),
                           UA_NS0ID(INT32),
                           UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE,
                           cb_ReadInt32, cb_WriteInt32);
        if (r != UA_STATUSCODE_GOOD) return r;
    }

    /* Relays: read/write booleans. */
    for (uint8_t i = 0; i < IO_RELAY_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "RLY_%02u", (unsigned)i);
        r = addCallbackVar(server, rlFolder, name, nodeId(ID_RELAY_BASE + i),
                           UA_NS0ID(BOOLEAN),
                           UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE,
                           cb_ReadBoolean, cb_WriteBoolean);
        if (r != UA_STATUSCODE_GOOD) return r;
    }

    /* Methods. */
    UA_MethodAttributes ma_reset = UA_MethodAttributes_default;
    ma_reset.displayName = lt("ResetCounter");
    ma_reset.description = lt("Resets the gateway's internal counter");
    ma_reset.executable   = true;
    ma_reset.userExecutable = true;
    r = UA_Server_addMethodNode(server,
                                UA_NODEID_NULL, mtFolder,
                                UA_NS0ID(HASCOMPONENT),
                                qname("ResetCounter"),
                                ma_reset, methodResetCounter,
                                0, NULL, 0, NULL,
                                NULL, NULL);
    if (r != UA_STATUSCODE_GOOD) return r;

    UA_MethodAttributes ma_estop = UA_MethodAttributes_default;
    ma_estop.displayName = lt("EmergencyStop");
    ma_estop.description = lt("Force all DOs and Relays to OFF");
    ma_estop.executable   = true;
    ma_estop.userExecutable = true;
    r = UA_Server_addMethodNode(server,
                                UA_NODEID_NULL, mtFolder,
                                UA_NS0ID(HASCOMPONENT),
                                qname("EmergencyStop"),
                                ma_estop, methodEmergencyStop,
                                0, NULL, 0, NULL,
                                NULL, NULL);
    if (r != UA_STATUSCODE_GOOD) return r;

    return UA_STATUSCODE_GOOD;
}
