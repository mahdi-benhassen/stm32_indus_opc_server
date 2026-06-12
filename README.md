# STM32F407 Industry 4.0 Gateway — OPC UA Server

FreeRTOS task that hosts an [open62541](https://open62541.org/) OPC UA server
on top of lwIP, exposing the gateway's digital I/O, analog I/O, relays and a
pair of method nodes (`ResetCounter`, `EmergencyStop`) to modern SCADA clients.

## Source layout

```
Inc/
  ua_config.h            open62541 build config (RAM-tuned for STM32F407)
  opcua_server_task.h    FreeRTOS task + thread-safe I/O accessor API
  opcua_node_model.h     Address-space layout (Industrial_IO folder, etc.)
Src/
  opcua_server_task.c    UA_Server_run_iterate() loop, shared I/O shadow
  opcua_node_model.c     Folder/variable/method creation + value callbacks
.github/workflows/
  build.yml              CI: ARM toolchain + open62541 amalgamation + compile
```

## Build

The project is consumed by the existing STM32CubeIDE build that already links
FreeRTOS, lwIP and the HAL drivers.  Add the amalgamated `open62541.c` /
`open62541.h` (v1.3.9) to your project with the preprocessor define
`UA_CONFIG_H_FILE="ua_config.h"`.

## CI

`.github/workflows/build.yml` runs on every push/PR and:

1. Installs the `arm-none-eabi-gcc` toolchain.
2. Downloads the open62541 v1.3.9 amalgamated release into `third_party/`.
3. Compiles the two application translation units with the same `-D` flags
   used locally, as a syntax-and-include sanity check.

## OPC UA address space

```
Objects/
  └─ Industrial_IO
       ├─ DigitalInputs  (DI_00..DI_07, Boolean, RO)
       ├─ DigitalOutputs (DO_00..DO_07, Boolean, RW)
       ├─ AnalogInputs   (AI_00..AI_07, Int32,   RO)
       ├─ AnalogOutputs  (AO_00..AO_07, Int32,   RW)
       ├─ Relays         (RLY_00..RLY_03, Boolean, RW)
       └─ Methods        (ResetCounter, EmergencyStop)
```

Default endpoint: `opc.tcp://<gateway-ip>:4840`, security policy `None`.
