/*
 * open62541_stub.h
 *
 * Minimal stand-in for the open62541 amalgamation header, used ONLY
 * by the CI sanity build.  It declares just enough of the OPC UA
 * public API that opcua_server_task.c and opcua_node_model.c can be
 * parsed and type-checked with arm-none-eabi-gcc.  The real
 * open62541.h from the amalgamation release replaces this file on
 * the STM32CubeIDE build.
 *
 * If you add a new open62541 API call to the application code, mirror
 * its prototype here or the CI build will fail.
 */

#ifndef OPEN62541_STUB_H_
#define OPEN62541_STUB_H_

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Primitive type aliases --------------------------------------------- */
typedef uint8_t  UA_Byte;
typedef int32_t  UA_Int32;
typedef int64_t  UA_Int64;
typedef uint32_t UA_UInt32;
typedef uint64_t UA_UInt64;
typedef uint16_t UA_UInt16;
typedef double   UA_Double;
typedef int      UA_Boolean;
typedef uint64_t UA_DateTime;
typedef void    *UA_NodeId;       /* opaque in the stub */
typedef void    *UA_NodeId_null;  /* placeholder, never used */

/* ---- Forward declarations for mutually-recursive structs ----------------- */
struct UA_DataType;
struct UA_DataValue;

/* ---- Simple aggregate types --------------------------------------------- */
typedef struct {
    void    *data;
    size_t   length;
} UA_String;

typedef struct {
    char    *locale;
    char    *text;
} UA_LocalizedText;

typedef struct {
    void    *data;
    size_t   length;
} UA_ByteString;

typedef struct {
    UA_UInt16 namespaceIndex;
    char     *name;
} UA_QualifiedName;

typedef struct {
    void     *data;
    size_t    length;
    UA_Byte   type;
} UA_Variant;

typedef struct { UA_UInt32 v[2]; } UA_Guid;

/* ---- DataValue (used by UA_ValueSource.read/write) ---------------------- */
typedef struct {
    UA_Variant    value;
    UA_Boolean    hasValue;
    UA_DateTime   sourceTimestamp;
    UA_UInt16     sourcePicoseconds;
    UA_UInt32     status;
} UA_DataValue;

/* ---- DataType (the .members array points back at UA_DataType) ----------- */
typedef struct UA_DataTypeMember {
    const char               *memberName;
    const struct UA_DataType  *memberType;
    UA_Byte                   padding;
    UA_Boolean                namespaceZero;
    size_t                    memberSize;
    UA_UInt32                 arrayDimensionsSize;
    UA_Boolean                isArray;
    UA_Boolean                isOptional;
} UA_DataTypeMember;

typedef struct UA_DataType {
    const char          *typeName;
    UA_NodeId            typeId;
    UA_UInt32            binaryEncodingId;
    size_t               memSize;
    size_t               typeKind;
    UA_Boolean           pointerFree;
    UA_Boolean           overlappable;
    size_t               membersSize;
    UA_DataTypeMember   *members;
} UA_DataType;

extern const UA_DataType UA_TYPES[1];

/* Type indices used by the application.  The real open62541 defines many
 * more - the stub only needs the slots we touch. */
#define UA_TYPES_BOOLEAN     0
#define UA_TYPES_INT32       0
#define UA_TYPES_INT64       0
#define UA_TYPES_UINT32      0
#define UA_TYPES_STRING      0
#define UA_TYPES_BYTESTRING  0

/* Variant helpers */
void       UA_Variant_setScalarCopy(UA_Variant *v, void *p, const UA_DataType *t);
UA_Boolean UA_Variant_isScalar(const UA_Variant *v);
UA_DateTime UA_DateTime_now(void);

/* ---- Strings / literals -------------------------------------------------- */
#define UA_STRING(CHARS)  { (void*)(CHARS), sizeof(CHARS)-1 }
#define UA_LOCALIZED_TEXT(LOCALE, TEXT) { (LOCALE), (TEXT) }
#define UA_NODEID_NUMERIC(NS, ID) ((UA_NodeId)0)
#define UA_NODEID_NULL            ((UA_NodeId)0)
#define UA_QUALIFIED_NAME(NS, NAME) { (NS), (NAME) }
#define UA_NS0ID_OBJECTSFOLDER    85
#define UA_NS0ID_ORGANIZES        35
#define UA_NS0ID_FOLDERTYPE       61
#define UA_NS0ID_HASCOMPONENT     47
#define UA_NS0ID_BOOLEAN          1
#define UA_NS0ID_INT32            6
#define UA_NS0ID_BASEDATAVARIABLETYPE 63

/* ---- Attributes defaults ------------------------------------------------- */
typedef struct {
    UA_UInt32         specifiedAttributes;
    UA_LocalizedText  displayName;
    UA_LocalizedText  description;
    UA_UInt32         writeMask;
} UA_ObjectAttributes;

typedef struct {
    UA_UInt32         specifiedAttributes;
    UA_LocalizedText  displayName;
    UA_LocalizedText  description;
    UA_UInt32         writeMask;
    UA_NodeId         dataType;
    UA_Int32          valueRank;
    UA_Variant        value;
    UA_Byte           accessLevel;
} UA_VariableAttributes;

#define UA_ObjectAttributes_default   ((UA_ObjectAttributes){0, {0,0}, {0,0}, 0})
#define UA_VariableAttributes_default ((UA_VariableAttributes){0, {0,0}, {0,0}, 0, UA_NODEID_NULL, 0, {0,0,0}, 0})

/* Access level mask */
#define UA_ACCESSLEVELMASK_READ   0x01
#define UA_ACCESSLEVELMASK_WRITE  0x02

/* Value rank */
#define UA_VALUERANK_SCALAR  (-1)

/* Status codes (subset) */
#define UA_STATUSCODE_GOOD  0x00000000

/* ---- Logger / server config --------------------------------------------- */
typedef void (*UA_LogFunction)(void *ctx, int level, int category,
                               const char *msg, va_list args);
typedef struct {
    UA_LogFunction   log;
    void            *context;
    int              minLevel;
    int              levels;
} UA_Logger;

typedef void *UA_Server;
typedef void *UA_ServerConfig;
typedef int   UA_StatusCode;

UA_ServerConfig *UA_ServerConfig_new_minimal(UA_UInt16 port, void *certificate);
void             UA_ServerConfig_setLogger (UA_ServerConfig *cfg, UA_Logger logger);
void             UA_ServerConfig_addSecurityPolicyNone(UA_ServerConfig *cfg, UA_String *policy);
UA_Server       *UA_Server_newWithConfig   (UA_ServerConfig *cfg);
void             UA_Server_delete          (UA_Server *server);
void             UA_Server_run_iterate     (UA_Server *server, UA_UInt16 timeoutMs);

/* ---- Node creation ------------------------------------------------------- */
UA_StatusCode UA_Server_addObjectNode  (UA_Server *s, UA_NodeId requestedId,
                                        UA_NodeId parent, UA_NodeId referenceTypeId,
                                        UA_QualifiedName browseName, UA_NodeId typeId,
                                        UA_ObjectAttributes attrs, void *nodeContext,
                                        UA_NodeId *outId);
UA_StatusCode UA_Server_addVariableNode(UA_Server *s, UA_NodeId requestedId,
                                        UA_NodeId parent, UA_NodeId referenceTypeId,
                                        UA_QualifiedName browseName, UA_NodeId typeId,
                                        UA_VariableAttributes attrs, void *nodeContext,
                                        UA_NodeId *outId);

typedef UA_StatusCode (*UA_MethodCallback)(UA_Server *s,
                                           const UA_NodeId *sessionId,
                                           void *sessionContext,
                                           const UA_NodeId *methodId,
                                           void *methodContext,
                                           size_t inputSize,
                                           const UA_Variant *input,
                                           size_t outputSize,
                                           UA_Variant *output);

UA_StatusCode UA_Server_addMethodNode  (UA_Server *s, UA_NodeId requestedId,
                                        UA_NodeId parent, UA_NodeId referenceTypeId,
                                        UA_QualifiedName browseName,
                                        UA_LocalizedText displayName,
                                        UA_LocalizedText description,
                                        UA_MethodCallback method,
                                        size_t inputSize, const void *inputArgs,
                                        size_t outputSize, const void *outputArgs,
                                        void *nodeContext, UA_NodeId *outId);

/* ---- Value source / callbacks ------------------------------------------- */
typedef struct {
    void (*read) (UA_Server*, const UA_NodeId*, UA_DataValue*, void*);
    void (*write)(UA_Server*, const UA_NodeId*, const UA_DataValue*, void*);
} UA_ValueSource;

void UA_Server_setVariableNode_valueSource(UA_Server *s, UA_NodeId id, UA_ValueSource vs);

/* Argument type for method nodes */
typedef struct {
    UA_NodeId         dataType;
    UA_Int32          valueRank;
    UA_LocalizedText  name;
    UA_LocalizedText  description;
} UA_Argument;
void UA_Argument_init(UA_Argument *a);

#ifdef __cplusplus
}
#endif

#endif /* OPEN62541_STUB_H_ */
