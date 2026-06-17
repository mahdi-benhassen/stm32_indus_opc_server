/*
 * main_ci.c
 *
 * Tiny main() for the CI host build.  Real target firmware does not
 * use this file - on the target the FreeRTOS scheduler owns main() and
 * OpcUaServer_Init() is called from the startup task before
 * vOpcUaServerTask is spawned.
 *
 * The CI main() does:
 *   1. calls OpcUaServer_Init()    -> builds the config + address space
 *   2. calls UA_Server_run_startup -> starts the binary protocol
 *                                     manager which opens the listening
 *                                     socket on :4840
 *   3. ticks UA_Server_run_iterate for ~3 seconds
 *   4. calls UA_Server_run_shutdown + UA_Server_delete
 *
 * The goal is to produce a real, runnable ELF that proves the open62541
 * server initialises, builds the address space, binds the socket on
 * :4840 and answers a real OPC UA HEL/ACK handshake.
 */

#include "opcua_server_task.h"
#include "open62541.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int main(void)
{
    int32_t rc = OpcUaServer_Init();
    if (rc != 0) {
        fprintf(stderr, "OpcUaServer_Init failed: %d\n", (int)rc);
        return 1;
    }
    printf("OPC UA server initialised.\n");

    UA_Server *server = (UA_Server *)OpcUaServer_GetHandle();
    if (!server) {
        fprintf(stderr, "OpcUaServer_GetHandle returned NULL\n");
        return 2;
    }

    /* Print the actual serverUrls the server was configured with. */
    UA_ServerConfig *cfg = UA_Server_getConfig(server);
    for (size_t i = 0; i < cfg->serverUrlsSize; i++) {
        char url[256] = {0};
        size_t n = cfg->serverUrls[i].length;
        if (n >= sizeof(url)) n = sizeof(url) - 1;
        memcpy(url, cfg->serverUrls[i].data, n);
        printf("Configured ServerUrl[%zu]: %s\n", i, url);
    }
    fflush(stdout);

    /* UA_Server_run_startup is what actually starts the binary protocol
     * manager (which opens the listening socket) and the rest of the
     * server components.  Without it the EventLoop thread is alive
     * but no listener is ever bound. */
    UA_StatusCode sr = UA_Server_run_startup(server);
    if (sr != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "UA_Server_run_startup failed: 0x%08x\n", (unsigned)sr);
        return 3;
    }
    printf("UA_Server_run_startup: OK\n");
    fflush(stdout);

    /* Tick the server for a few seconds.  Pass waitInternal=true so
     * the EventLoop can actually accept connections. */
    time_t deadline = time(NULL) + 3;
    while (time(NULL) < deadline) {
        UA_Server_run_iterate(server, true);
    }

    printf("OPC UA server ran for 3 seconds; shutting down.\n");
    OpcUaServer_Stop();
    OpcUaServer_Destroy();
    return 0;
}
