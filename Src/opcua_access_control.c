/*
 * opcua_access_control.c
 *
 * Username/password access control plugin for the OPC UA server.
 *
 * Uses open62541's UA_AccessControl_default with a compiled-in
 * credential list.  On the STM32 target, credentials should be stored
 * in persistent memory (flash sector or EEPROM) and loaded at startup.
 */

#include "opcua_access_control.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* Credential table                                                            */
/*                                                                            */
/* UA_UsernamePasswordLogin is { UA_String username; UA_String password; }    */
/* in open62541 v1.5.4.  We use UA_STRING_STATIC so no heap allocation is     */
/* needed for the credential strings.                                         */
/* -------------------------------------------------------------------------- */
static const UA_UsernamePasswordLogin s_logins[] = {
    { UA_STRING_STATIC(OPCUA_DEFAULT_USER),
      UA_STRING_STATIC(OPCUA_DEFAULT_PASSWORD) }
};
static const size_t s_numLogins =
    sizeof(s_logins) / sizeof(s_logins[0]);

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */
void OpcUa_AccessControl_Configure(UA_ServerConfig *cfg)
{
    if (!cfg) return;

    /* UA_AccessControl_default installs the standard access control
     * callbacks and enforces username/password authentication using
     * the provided login table.  Pass allowAnonymous=false to reject
     * anonymous connections. */
    UA_StatusCode r = UA_AccessControl_default(
        cfg,
        false,              /* anonymous login NOT allowed */
        NULL,               /* no custom user token policy URI */
        s_numLogins,
        s_logins);

    if (r != UA_STATUSCODE_GOOD) {
        /* If access control setup fails, log it.  In production,
         * this should be treated as a fatal error. */
    }
}
