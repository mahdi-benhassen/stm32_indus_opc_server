/*
 * opcua_node_model.h
 *
 * Information Model for the Industry 4.0 gateway.  Exposes a single
 * top-level folder "Industrial_IO" containing four sub-folders (DI / DO /
 * AI / AO) plus a Relays folder and a Methods folder.
 *
 *   Objects/
 *     └─ Industrial_IO  (Object)
 *          ├─ DigitalInputs   (Folder)
 *          │    ├─ DI_00 .. DI_07  (Boolean, RO)
 *          ├─ DigitalOutputs  (Folder)
 *          │    ├─ DO_00 .. DO_07  (Boolean, RW)
 *          ├─ AnalogInputs    (Folder)
 *          │    ├─ AI_00 .. AI_07  (Int32,  RO)
 *          ├─ AnalogOutputs   (Folder)
 *          │    ├─ AO_00 .. AO_07  (Int32,  RW)
 *          ├─ Relays          (Folder)
 *          │    ├─ RLY_00 .. RLY_03 (Boolean, RW)
 *          └─ Methods
 *               ├─ ResetCounter (Method, 0 in / 0 out)
 *               └─ EmergencyStop(Method, 0 in / 0 out)
 */

#ifndef OPCUA_NODE_MODEL_H_
#define OPCUA_NODE_MODEL_H_

#include "open62541.h"

#define IO_DI_COUNT    8
#define IO_DO_COUNT    8
#define IO_AI_COUNT    8
#define IO_AO_COUNT    8
#define IO_RELAY_COUNT 4

/* Namespace URI for the Industrial_IO model.  Registered with the server
 * at build time; the actual namespace index is resolved at runtime. */
#define IO_NAMESPACE_URI  "urn:stm32_indus_opc_server:Industrial_IO"

/* Build the entire address space and attach value-callbacks to each
 * variable.  Called from OpcUaServer_Init() before the server is run.
 * Returns UA_STATUSCODE_GOOD on success, an error code on failure (any
 * partially-built nodes are cleaned up when the caller deletes the server). */
UA_StatusCode OpcUaNodeModel_Build(UA_Server *server);

#endif /* OPCUA_NODE_MODEL_H_ */
