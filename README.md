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
  cmsis_os_stubs.c       CI-only stubs; on the target CMSIS-RTOS2 is linked
.github/workflows/
  build.yml              CI: host compile + open62541 v1.5.4 amalgamation
```

## Build

The project is consumed by the existing STM32CubeIDE build that already
links FreeRTOS, lwIP and the HAL drivers.  Add the amalgamated
`open62541.c` / `open62541.h` (v1.5.4) to the project and define:

```
-DOPCUA_EMBEDDED_TARGET=1
-DUA_CONFIG_H_FILE="ua_config.h"
```

in the C preprocessor symbols.  The `OPCUA_EMBEDDED_TARGET` switch routes
the build to the FreeRTOS / lwIP / CMSIS code paths instead of the host
stubs.

### CMake command line (alternative to STM32CubeIDE)

```bash
arm-none-eabi-gcc -c -mcpu=cortex-m4 -mfloat-abi=soft -mthumb \
    -DOPCUA_EMBEDDED_TARGET=1 \
    -DUA_CONFIG_H_FILE='"ua_config.h"' \
    -I Inc -I third_party/open62541 \
    -ffunction-sections -fdata-sections \
    Src/opcua_server_task.c Src/opcua_node_model.c \
    third_party/open62541/open62541.c \
    -o firmware.elf
```

## Flashing the STM32F407

The CI job builds and tests the source on a host compiler, but it does NOT
produce a flashable binary for the target.  The final `firmware.elf` (or
the matching `.bin` / `.hex`) is produced by the STM32CubeIDE project.  The
recommended paths to get it onto the board are below.

### 1. From STM32CubeIDE

1. Right-click the project → **Build Configurations** → **Release** (or
   whatever configuration includes this source tree).
2. **Project** → **Build Project**.  The output is
   `<Project>/Release/<ProjectName>.elf` and `<ProjectName>.bin`.
3. **Run** → **Debug** (`F11`).  The IDE uses the on-board ST-LINK to
   erase, program and reset the target.

### 2. From the command line using OpenOCD + ST-LINK

The Discovery / Nucleo on-board ST-LINK (V2-1 or V3) is wired to PA13/PA14.
Connect the USB and use OpenOCD:

```bash
# Locate the freshly built ELF
ELF=build/Release/stm32_indus_opc_server.elf

# Erase, flash, verify, reset - all in one shot
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
        -c "program ${ELF} verify reset exit"
```

If you also want a raw binary for an OEM boot loader:

```bash
arm-none-eabi-objcopy -O binary ${ELF} firmware.bin
```

### 3. Using STM32CubeProgrammer

```bash
STM32_Programmer_CLI -c port=SWD -w firmware.bin 0x08000000 -v -rst
```

(`0x08000000` is the start of the STM32F407's 1 MB flash.)

### 4. Via the on-board ST-LINK in DFU mode

If the on-board ST-LINK is unavailable (e.g. it has been re-flashed or
isolated), put the target into System Bootloader mode (BOOT0 = 1, reset)
and flash over USART1 with `stm32flash`:

```bash
stm32flash -w firmware.bin -v -g 0x0 /dev/ttyUSB0
```

Return BOOT0 to 0 and reset.

### Boot pins (post-flash)

| BOOT0 | BOOT1 | Boot from              |
|-------|-------|------------------------|
| 0     | x     | User flash (normal)    |
| 1     | 0     | System bootloader (USART/USB DFU) |
| 1     | 1     | Embedded SRAM          |

## Verifying the OPC UA server is alive

After flashing, give the gateway 2-3 seconds to bring up lwIP, then from any
host on the same network:

```bash
# Quick TCP probe - the server listens on 4840
nc -zv <gateway-ip> 4840

# Full OPC UA discovery using open62541's ua-cli
ua-cli discover opc.tcp://<gateway-ip>:4840
```

You should see an endpoint with:

* **Endpoint URL**: `opc.tcp://<gateway-ip>:4840`
* **Security Policy**: `http://opcfoundation.org/UA/SecurityPolicy#None`
* **Security Mode**: `None`

Browse to `Objects ▸ Industrial_IO` to see DI_00..DI_07, DO_00..DO_07,
AI_00..AI_07, AO_00..AO_07, RLY_00..RLY_03, and the `Methods` folder.

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

## CI

`.github/workflows/build.yml` runs on every push/PR and:

1. Installs `build-essential`, `libmbedtls-dev`.
2. Downloads the open62541 v1.5.4 amalgamated release into
   `third_party/`.
3. Compiles the amalgamation plus the two application translation units
   on a host `gcc` and partially links them with `ld -r`.  This catches
   type / signature mismatches between our code and the amalgamation
   without needing the full FreeRTOS / lwIP stack on the runner.
4. Uploads the resulting `.o` files as build artifacts.
