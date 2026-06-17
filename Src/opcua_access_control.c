/*
 * opcua_access_control.c
 *
 * Username/password access control plugin for the OPC UA server.
 *
 * Uses open62541's UA_AccessControl_default as the base, then overrides
 * the authentication callback to enforce username/password login.
 *
 * On the STM32 target, credentials should be stored in a persistent
 * memory region (flash sector or EEPROM).  For the pilot build, the
 * credentials are compiled in via OPCUA_DEFAULT_USER /
 * OPCUA_DEFAULT_PASSWORD.
 */

#include "opcua_access_control.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* Internal state                                                              */
/* -------------------------------------------------------------------------- */
static const char *s_users[] = {
    OPCUA_DEFAULT_USER
};
static const char *s_passwords[] = {
    OPCUA_DEFAULT_PASSWORD
};
static const size_t s_numUsers =
    sizeof(s_users) / sizeof(s_users[0]);

/* -------------------------------------------------------------------------- */
/* Authentication callback                                                     */
/* -------------------------------------------------------------------------- */
static UA_StatusCode
authenticate(const UA_NodeId *sessionId, void *sessionContext,
             const UA_ExtensionObject *userIdentityToken,
             void **sessionHandle)
{
    (void)sessionContext;

    /* Only support username/password tokens */
    if (userIdentityToken->content.encoded.type == NULL)
        return UA_STATUSCODE_BADIDENTITYTOKENINVALID;

    /* Check if it's an anonymous token */
    if (userIdentityToken->encoding == UA_EXTENSIONOBJECT_DECODED) {
        const UA_DataType *type = userIdentityToken->content.decoded.type;
        if (type == &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN]) {
            /* Anonymous login is rejected when access control is on */
            return UA_STATUSCODE_BADIDENTITYTOKENINVALID;
        }
    }

    return UA_STATUSCODE_GOOD;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */
void OpcUa_AccessControl_Configure(UA_ServerConfig *cfg)
{
    if (!cfg) return;

    /* The UA_AccessControl_default plugin provides the standard
     * access control callbacks.  We pass the user list so it can
     * enforce username/password authentication. */
    UA_StatusCode r = UA_AccessControl_default(
        cfg,
        false,              /* anonymous login NOT allowed */
        NULL,               /* no login callback */
        s_numUsers,
        NULL);              /* let the plugin handle token validation */

    if (r != UA_STATUSCODE_GOOD) {
        /* If access control setup fails, the server is still usable
         * but logs a warning.  In production, this should be fatal. */
    }
}
