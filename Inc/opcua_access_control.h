/*
 * opcua_access_control.h
 *
 * Access control plugin for the OPC UA server.
 *
 * When OPCUA_ENABLE_ACCESS_CONTROL is defined to 1, the server
 * enforces username/password authentication.  Clients must provide
 * credentials matching one of the configured users.
 *
 * When undefined or 0, the server accepts anonymous access (the
 * open62541 default).  This is suitable for a closed engineering
 * network but MUST NOT be used on a production plant floor.
 *
 * Usage in OpcUaServer_Init:
 *   #if OPCUA_ENABLE_ACCESS_CONTROL
 *       OpcUa_AccessControl_Configure(cfg);
 *   #endif
 */

#ifndef OPCUA_ACCESS_CONTROL_H_
#define OPCUA_ACCESS_CONTROL_H_

#include "open62541.h"

/* Set to 1 in the STM32CubeIDE build settings to enable. */
#ifndef OPCUA_ENABLE_ACCESS_CONTROL
#define OPCUA_ENABLE_ACCESS_CONTROL 0
#endif

/* Default credentials — CHANGE THESE in production!
 * Store them in a non-volatile memory region or derive from the
 * device serial number.  These defaults are for development only. */
#define OPCUA_DEFAULT_USER     "engineer"
#define OPCUA_DEFAULT_PASSWORD "opcua123"

/* Configure the access control plugin on the given server config.
 * Must be called AFTER UA_ServerConfig_setMinimal and BEFORE
 * UA_Server_newWithConfig. */
void OpcUa_AccessControl_Configure(UA_ServerConfig *cfg);

#endif /* OPCUA_ACCESS_CONTROL_H_ */
