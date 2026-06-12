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
#define NS_IO         1
#define ID_DI_BASE    0x1000
#define ID_DO_BASE    0x2000
#define ID_AI_BASE    0x3000
#define ID_AO_BASE    0x4000
#define ID_RELAY_BASE 0x5000

static UA_NodeId
nodeId(uint16_t id)
{
    return UA_NODEID_NUMERIC(NS_IO, id);
}

static UA_QualifiedName
qname(const char *name)
{
    return UA_QUALIFIEDNAME(NS_IO, (char *)name);
}

static UA_LocalizedText
lt(const char *text)
{
    return UA_LOCALIZEDTEXT("en-US", (char *)text);
}

/* ----------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------------- */
static UA_NodeId
addObjectFolder(UA_Server *s, UA_NodeId parent, const char *browseName)
{
    UA_ObjectAttributes oa = UA_ObjectAttributes_default;
    oa.displayName = lt(browseName);
    oa.description = lt(browseName);
    UA_NodeId id;
    UA_Server_addObjectNode(s,
                            UA_NODEID_NULL,
                            parent,
                            UA_NS0ID(ORGANIZES),
                            qname(browseName),
                            UA_NS0ID(FOLDERTYPE),
                            oa, NULL, &id);
    return id;
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
    if (nodeId->namespaceIndex == NS_IO) {
        if (nodeId->identifier.numeric >= ID_DI_BASE &&
            nodeId->identifier.numeric <  ID_DI_BASE + IO_DI_COUNT) {
            v = OpcUa_Hw_ReadDI((uint8_t)(nodeId->identifier.numeric - ID_DI_BASE)) ? true : false;
        } else if (nodeId->identifier.numeric >= ID_RELAY_BASE &&
                   nodeId->identifier.numeric <  ID_RELAY_BASE + IO_RELAY_COUNT) {
            v = OpcUa_Hw_ReadRelay((uint8_t)(nodeId->identifier.numeric - ID_RELAY_BASE)) ? true : false;
        }
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
    if (nodeId->namespaceIndex != NS_IO) return UA_STATUSCODE_BADNODEIDUNKNOWN;
    if (nodeId->identifier.numeric >= ID_DO_BASE &&
        nodeId->identifier.numeric <  ID_DO_BASE + IO_DO_COUNT) {
        OpcUa_Hw_WriteDO((uint8_t)(nodeId->identifier.numeric - ID_DO_BASE), v ? 1 : 0);
    } else if (nodeId->identifier.numeric >= ID_RELAY_BASE &&
               nodeId->identifier.numeric <  ID_RELAY_BASE + IO_RELAY_COUNT) {
        OpcUa_Hw_WriteRelay((uint8_t)(nodeId->identifier.numeric - ID_RELAY_BASE),
                            v ? 1 : 0);
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
    if (nodeId->namespaceIndex == NS_IO &&
        nodeId->identifier.numeric >= ID_AI_BASE &&
        nodeId->identifier.numeric <  ID_AI_BASE + IO_AI_COUNT) {
        v = OpcUa_Hw_ReadAI((uint8_t)(nodeId->identifier.numeric - ID_AI_BASE));
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
    if (nodeId->namespaceIndex != NS_IO) return UA_STATUSCODE_BADNODEIDUNKNOWN;
    if (nodeId->identifier.numeric < ID_AO_BASE ||
        nodeId->identifier.numeric >= ID_AO_BASE + IO_AO_COUNT)
        return UA_STATUSCODE_BADNODEIDUNKNOWN;

    OpcUa_Hw_WriteAO((uint8_t)(nodeId->identifier.numeric - ID_AO_BASE), v);
    return UA_STATUSCODE_GOOD;
}

/* ----------------------------------------------------------------------------
 * Variable-node creation helper
 * ---------------------------------------------------------------------------- */
static void
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
    if (UA_NodeId_equal(&typeId, &UA_NS0ID(BOOLEAN))) {
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
    UA_Server_addCallbackValueSourceVariableNode(
        s, requestedId, parent,
        UA_NS0ID(HASCOMPONENT), qname(name), UA_NS0ID(BASEDATAVARIABLETYPE),
        va, cvs, NULL, &outId);
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
    extern void Counter_Reset(void);
    Counter_Reset();
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
    for (uint8_t i = 0; i < IO_DO_COUNT;   i++) OpcUa_Hw_WriteDO(i, 0);
    for (uint8_t i = 0; i < IO_RELAY_COUNT; i++) OpcUa_Hw_WriteRelay(i, 0);
    return UA_STATUSCODE_GOOD;
}

/* ----------------------------------------------------------------------------
 * Public entry point
 * ---------------------------------------------------------------------------- */
UA_StatusCode OpcUaNodeModel_Build(UA_Server *server)
{
    /* Top-level folder under Objects/. */
    UA_NodeId industrialIO = addObjectFolder(server,
                                             UA_NS0ID(OBJECTSFOLDER),
                                             "Industrial_IO");

    UA_NodeId diFolder = addObjectFolder(server, industrialIO, "DigitalInputs");
    UA_NodeId doFolder = addObjectFolder(server, industrialIO, "DigitalOutputs");
    UA_NodeId aiFolder = addObjectFolder(server, industrialIO, "AnalogInputs");
    UA_NodeId aoFolder = addObjectFolder(server, industrialIO, "AnalogOutputs");
    UA_NodeId rlFolder = addObjectFolder(server, industrialIO, "Relays");
    UA_NodeId mtFolder = addObjectFolder(server, industrialIO, "Methods");

    /* DI: read-only booleans. */
    for (uint8_t i = 0; i < IO_DI_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "DI_%02u", (unsigned)i);
        addCallbackVar(server, diFolder, name, nodeId(ID_DI_BASE + i),
                       UA_NS0ID(BOOLEAN),
                       UA_ACCESSLEVELMASK_READ,
                       cb_ReadBoolean, NULL);
    }

    /* DO: read/write booleans. */
    for (uint8_t i = 0; i < IO_DO_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "DO_%02u", (unsigned)i);
        addCallbackVar(server, doFolder, name, nodeId(ID_DO_BASE + i),
                       UA_NS0ID(BOOLEAN),
                       UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE,
                       cb_ReadBoolean, cb_WriteBoolean);
    }

    /* AI: read-only Int32. */
    for (uint8_t i = 0; i < IO_AI_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "AI_%02u", (unsigned)i);
        addCallbackVar(server, aiFolder, name, nodeId(ID_AI_BASE + i),
                       UA_NS0ID(INT32),
                       UA_ACCESSLEVELMASK_READ,
                       cb_ReadInt32, NULL);
    }

    /* AO: read/write Int32. */
    for (uint8_t i = 0; i < IO_AO_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "AO_%02u", (unsigned)i);
        addCallbackVar(server, aoFolder, name, nodeId(ID_AO_BASE + i),
                       UA_NS0ID(INT32),
                       UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE,
                       cb_ReadInt32, cb_WriteInt32);
    }

    /* Relays: read/write booleans. */
    for (uint8_t i = 0; i < IO_RELAY_COUNT; i++) {
        char name[12];
        snprintf(name, sizeof(name), "RLY_%02u", (unsigned)i);
        addCallbackVar(server, rlFolder, name, nodeId(ID_RELAY_BASE + i),
                       UA_NS0ID(BOOLEAN),
                       UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE,
                       cb_ReadBoolean, cb_WriteBoolean);
    }

    /* Methods. */
    UA_MethodAttributes ma_reset = UA_MethodAttributes_default;
    ma_reset.displayName = lt("ResetCounter");
    ma_reset.description = lt("Resets the gateway's internal counter");
    ma_reset.executable   = true;
    ma_reset.userExecutable = true;
    UA_Server_addMethodNode(server,
                            UA_NODEID_NULL, mtFolder,
                            UA_NS0ID(HASCOMPONENT),
                            qname("ResetCounter"),
                            ma_reset, methodResetCounter,
                            0, NULL, 0, NULL,
                            NULL, NULL);

    UA_MethodAttributes ma_estop = UA_MethodAttributes_default;
    ma_estop.displayName = lt("EmergencyStop");
    ma_estop.description = lt("Force all DOs and Relays to OFF");
    ma_estop.executable   = true;
    ma_estop.userExecutable = true;
    UA_Server_addMethodNode(server,
                            UA_NODEID_NULL, mtFolder,
                            UA_NS0ID(HASCOMPONENT),
                            qname("EmergencyStop"),
                            ma_estop, methodEmergencyStop,
                            0, NULL, 0, NULL,
                            NULL, NULL);

    return UA_STATUSCODE_GOOD;
}
