/*
 * main_ci.c
 *
 * Tiny main() for the CI host build.  Real target firmware does not
 * use this file - on the target the FreeRTOS scheduler owns main() and
 * OpcUaServer_Init() is called from the startup task before
 * vOpcUaServerTask is spawned.
 *
 * The CI main() just:
 *   1. calls OpcUaServer_Init()
 *   2. ticks UA_Server_run_iterate() for ~3 seconds (long enough for
 *      the server to bind the TCP listener on 4840)
 *   3. calls OpcUaServer_Stop() and exits cleanly
 *
 * The goal is to produce a real, runnable ELF that proves the open62541
 * server initialises, builds the address space, and binds the socket -
 * not to actually serve a real OPC UA client.
 */

#include "opcua_server_task.h"
#include "open62541.h"

#include <stdio.h>
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

    /* Print the actual serverUrls the server was configured with, so
     * the CI log shows what port the EventLoop was told to bind. */
    UA_ServerConfig *cfg = UA_Server_getConfig(server);
    for (size_t i = 0; i < cfg->serverUrlsSize; i++) {
        char url[256] = {0};
        size_t n = cfg->serverUrls[i].length;
        if (n >= sizeof(url)) n = sizeof(url) - 1;
        memcpy(url, cfg->serverUrls[i].data, n);
        printf("Configured ServerUrl[%zu]: %s\n", i, url);
    }
    fflush(stdout);
    if (!server) {
        fprintf(stderr, "OpcUaServer_GetHandle returned NULL\n");
        return 2;
    }

    /* Tick the server for a few seconds so it has a chance to bind
     * the TCP socket.  Pass waitInternal=true so the EventLoop has
     * time to do its initial bind of the listening sockets. */
    time_t deadline = time(NULL) + 3;
    while (time(NULL) < deadline) {
        /* In v1.5.4: (server, waitInternal).  true = block up to 500ms
         * per iterate so the EventLoop can actually accept connections
         * and run its initial bind. */
        UA_Server_run_iterate(server, true);
    }

    printf("OPC UA server ran for 3 seconds; shutting down.\n");
    OpcUaServer_Stop();
    return 0;
}
