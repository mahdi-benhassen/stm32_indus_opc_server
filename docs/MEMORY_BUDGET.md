# Memory Budget — STM32F407 OPC UA Server

## Hardware constraints

| Resource | Capacity | Notes |
|----------|----------|-------|
| Flash    | 1 MB     | Program code + constants |
| RAM      | 192 KB   | Heap + stacks + BSS + open62541 nodestore |
| CCM RAM  | 64 KB    | Optional: FreeRTOS stacks (if mapped) |

## RAM budget (192 KB total)

| Component | Est. RAM | Notes |
|-----------|----------|-------|
| FreeRTOS kernel + IDLE stack | 1 KB | |
| FreeRTOS heap_4 (shared) | 120 KB | Main allocation pool — lwIP, open62541, app |
| lwIP TCP/IP stack | 20 KB | pbufs, netconns, socket set |
| OPC UA task stack (16 KB) | 16 KB | `OPCUA_TASK_STACK_BYTES` |
| Modbus task stack | 4 KB | Existing Modbus task |
| Default task stack | 2 KB | Existing init task |
| Interrupt stacks | 1 KB | SVC/PendSV/SysTick |
| **Subtotal (fixed)** | **164 KB** | |
| **Available for heap growth** | **28 KB** | Headroom for open62541 nodestore + sessions |

### open62541 heap usage breakdown (from heap_4)

| Component | Est. RAM | Config knob |
|-----------|----------|-------------|
| Nodestore (256 nodes) | 12 KB | `UA_NODESTORE_MAXSIZE=256` |
| Per-session state (4 max) | 8 KB | `UA_MAXSESSIONCOUNT=4` |
| Per-SecureChannel (4 max) | 4 KB | `UA_MAXSECURECHANNELS=4` |
| Message buffers (16 KB × 4 conns) | 16 KB | `UA_MAXMESSAGEBYTESTOBUFFERED=16384` |
| Subscriptions (4 max, 128 items) | 4 KB | `UA_MAXSUBSCRIPTIONCOUNT=4` |
| Logger + server object strings | 2 KB | `UA_MAXSTRINGLENGTH=256` |
| **Total open62541** | **~46 KB** | |

### Verification

After building the full firmware with STM32CubeIDE, run:

```bash
arm-none-eabi-size --format=berkeley firmware.elf
```

Expected output:
```
   text	   data	    bss
 450000	  12000	 120000
```

- `text + data` must be < 1 MB (Flash).
- `bss` must be < 160 KB (RAM, excluding heap).
- `data` is copied from Flash to RAM at startup.

### Runtime verification

Add this debug code to the OPC UA task (every 60 seconds):

```c
size_t freeHeap = xPortGetFreeHeapSize();
size_t minHeap  = xPortGetMinimumEverFreeHeapSize();
UBaseType_t stackHW = uxTaskGetStackHighWaterMark(NULL);

Log_Print("RAM: free=", ...);
Log_Print("  Free heap:      ", freeHeap, " bytes");
Log_Print("  Min-ever heap:  ", minHeap,  " bytes");
Log_Print("  Stack HW:       ", stackHW * sizeof(StackType_t), " bytes");
```

### Pass criteria

| Metric | Minimum | Target |
|--------|--------|--------|
| Free heap after 1 hour | 15 KB | 25 KB |
| Min-ever free heap | 10 KB | 20 KB |
| OPC UA stack high-water | 2 KB | 4 KB |
| Flash usage | < 900 KB | < 700 KB |

### If RAM is too tight

1. Reduce `UA_MAXSESSIONCOUNT` to 2.
2. Reduce `UA_MAXMESSAGEBYTESTOBUFFERED` to 8192 (OPC UA minimum).
3. Reduce `UA_NODESTORE_MAXSIZE` to 128.
4. Move the OPC UA task stack to CCM RAM (64 KB region, not used by DMA).
5. Disable `UA_ENABLE_SUBSCRIPTIONS` if DataChange push is not needed.
