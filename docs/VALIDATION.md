# Validation Plan — STM32F407 OPC UA Server

This document defines the full validation procedure for the OPC UA server
before deployment to an industrial environment.  Each phase must be
signed off by the validation engineer.

---

## Table of Contents

1. [Phase 1: Desktop Validation](#phase-1-desktop-validation)
2. [Phase 2: Hardware-in-the-Loop](#phase-2-hardware-in-the-loop)
3. [Phase 3: Stress Testing](#phase-3-stress-testing)
4. [Phase 4: Security Hardening](#phase-4-security-hardening)
5. [Phase 5: Factory Acceptance Test](#phase-5-factory-acceptance-test)
6. [Sign-off Checklist](#sign-off-checklist)

---

## Phase 1: Desktop Validation

**Goal**: Verify the server compiles, links, starts, and exposes the
correct address space — all on a development PC without hardware.

**Duration**: 1–2 days.

### 1.1 Build the host ELF

```bash
# From the project root:
mkdir -p third_party/open62541
curl -fsSL -o third_party/open62541/open62541.c \
  https://github.com/open62541/open62541/releases/download/v1.5.4/open62541.c
curl -fsSL -o third_party/open62541/open62541.h \
  https://github.com/open62541/open62541/releases/download/v1.5.4/open62541.h

CFLAGS="-c -O0 -g -DUA_CONFIG_H_FILE='\"ua_config.h\"' \
        -I Inc -I third_party/open62541 -Wno-implicit-function-declaration"
gcc $CFLAGS Src/opcua_server_task.c -o /tmp/opcua_server_task.o
gcc $CFLAGS Src/opcua_node_model.c   -o /tmp/opcua_node_model.o
gcc $CFLAGS Src/main_ci.c            -o /tmp/main_ci.o
gcc $CFLAGS Src/opcua_access_control.c -o /tmp/opcua_access_control.o
gcc -c -O0 -g -I Inc -DUA_CONFIG_H_FILE='"ua_config.h"' -I third_party/open62541 \
  Src/cmsis_os_stubs.c -o /tmp/cmsis_os_stubs.o
gcc -c -O0 -g -DUA_CONFIG_H_FILE='"../../Inc/ua_config.h"' \
  -I . -I ../../Inc -Wno-implicit-function-declaration \
  third_party/open62541/open62541.c -o /tmp/open62541.o
gcc -o /tmp/opcua_server /tmp/*.o -lpthread -lm -lmbedtls -lmbedcrypto -lmbedx509
```

- [ ] **D1**: Build succeeds with zero errors.
- [ ] **D2**: `file /tmp/opcua_server` reports `ELF 64-bit LSB executable`.
- [ ] **D3**: `size /tmp/opcua_server` text < 5 MB.

### 1.2 Run the Python integration test suite

```bash
# Install the asyncua OPC UA client library
pip3 install asyncua

# Start the server
/tmp/opcua_server &

# Run the tests
python3 tests/test_opcua_server.py
kill %1
```

- [ ] **D4**: All tests in `tests/test_opcua_server.py` pass.
  - Server connects and returns endpoints.
  - Browse returns `Industrial_IO` folder.
  - Browse returns all 6 sub-folders (DigitalInputs, DigitalOutputs,
    AnalogInputs, AnalogOutputs, Relays, Methods).
  - Read `DI_00` returns a Boolean.
  - Read `DO_00` returns a Boolean.
  - Write `DO_00 = true`, read back = `true`.
  - Write `DO_00 = false`, read back = `false`.
  - Read `AI_00` returns an Int32.
  - Write `AO_00 = 12345`, read back = `12345`.
  - Read `RLY_00` returns a Boolean.
  - Write `RLY_00 = true`, read back = `true`.
  - Call `ResetCounter` method — returns `Good`.
  - Call `EmergencyStop` method — returns `Good`, all DOs = `false`,
    all Relays = `false`.
  - Subscription on `DI_00` pushes at least one DataChange notification
    within 1 second.

### 1.3 OPC UA compliance quick check

```bash
# Install open62541's CLI tool
pip3 install open62541-cli

ua-cli discover opc.tcp://127.0.0.1:4840
ua-cli browse opc.tcp://127.0.0.1:4840 Objects
ua-cli read opc.tcp://127.0.0.1:4840 "ns=2;s=Industrial_IO.DigitalInputs.DI_00"
```

- [ ] **D5**: `discover` returns one endpoint with SecurityPolicy#None.
- [ ] **D6**: `browse` returns the Industrial_IO folder with 6 children.
- [ ] **D7**: `read` returns a Boolean value.

---

## Phase 2: Hardware-in-the-Loop

**Goal**: Verify the server runs on the actual STM32F407 hardware and
that OPC UA reads/writes map correctly to physical I/O.

**Duration**: 3–5 days.

### 2.1 Flash the firmware

```bash
arm-none-eabi-objcopy -O binary firmware.elf firmware.bin
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
        -c "program firmware.elf verify reset exit"
```

- [ ] **H1**: OpenOCD reports `verified`.
- [ ] **H2**: Board's user LED blinks (or RTT log shows
      `UA_Server_run_startup: OK`).
- [ ] **H3**: `ping <board-ip>` succeeds.
- [ ] **H4**: `nc -zv <board-ip> 4840` reports `open`.

### 2.2 Address space verification (UaExpert)

Connect [UaExpert](https://www.unified-automation.com/products/development-tools/uaexpert.html)
to `opc.tcp://<board-ip>:4840`.

- [ ] **H5**: Connection succeeds with SecurityPolicy#None.
- [ ] **H6**: Browse tree shows `Objects ▸ Industrial_IO`.
- [ ] **H7**: `Industrial_IO` has 6 children: DigitalInputs,
      DigitalOutputs, AnalogInputs, AnalogOutputs, Relays, Methods.
- [ ] **H8**: `DigitalInputs` has 8 children: DI_00 … DI_07, all
      Boolean, read-only.
- [ ] **H9**: `DigitalOutputs` has 8 children: DO_00 … DO_07, all
      Boolean, read/write.
- [ ] **H10**: `AnalogInputs` has 8 children: AI_00 … AI_07, all
       Int32, read-only.
- [ ] **H11**: `AnalogOutputs` has 8 children: AO_00 … AO_07, all
       Int32, read/write.
- [ ] **H12**: `Relays` has 4 children: RLY_00 … RLY_03, all
       Boolean, read/write.
- [ ] **H13**: `Methods` has 2 children: ResetCounter, EmergencyStop.

### 2.3 I/O mapping verification

For each channel, verify that the OPC UA value matches the physical I/O.

**Digital Inputs** (connect a jumper wire to 3V3 / GND on each input):

| Test | Action | Expected OPC UA Value | Pass? |
|------|--------|-----------------------|-------|
| H14  | DI_00 → GND | `false` | |
| H15  | DI_00 → 3V3 | `true`  | |
| H16  | DI_01 → GND | `false` | |
| H17  | DI_01 → 3V3 | `true`  | |
| H18  | DI_07 → 3V3 | `true`  | |

**Digital Outputs** (write from UaExpert, observe the LED/relay):

| Test | Write | Expected Physical State | Pass? |
|------|-------|-------------------------|-------|
| H19  | DO_00 = true  | LED/DO_00 ON  | |
| H20  | DO_00 = false | LED/DO_00 OFF | |
| H21  | DO_07 = true  | LED/DO_07 ON  | |
| H22  | DO_07 = false | LED/DO_07 OFF | |

**Analog Inputs** (apply a known voltage via a signal generator):

| Test | Input | Expected OPC UA Value (±5%) | Pass? |
|------|-------|------------------------------|-------|
| H23  | AI_00 = 0 V    | 0     | |
| H24  | AI_00 = 1.65 V | ~2048 | |
| H25  | AI_00 = 3.3 V  | ~4095 | |
| H26  | AI_07 = 3.3 V  | ~4095 | |

**Analog Outputs** (write from UaExpert, measure with a voltmeter):

| Test | Write | Expected Voltage (±5%) | Pass? |
|------|-------|--------------------------|-------|
| H27  | AO_00 = 0     | 0 V    | |
| H28  | AO_00 = 2048  | 1.65 V | |
| H29  | AO_00 = 4095  | 3.3 V  | |

**Relays**:

| Test | Write | Expected Physical State | Pass? |
|------|-------|-------------------------|-------|
| H30  | RLY_00 = true  | Relay clicks ON  | |
| H31  | RLY_00 = false | Relay clicks OFF | |
| H32  | RLY_03 = true  | Relay clicks ON  | |

### 2.4 Method verification

| Test | Action | Expected Result | Pass? |
|------|--------|-----------------|-------|
| H33  | Set DO_03 = true, DO_05 = true, RLY_01 = true.  Call `EmergencyStop`. | All DOs = false, all Relays = false within 50 ms. | |
| H34  | Call `ResetCounter`. | Returns `Good`. | |

### 2.5 Subscription verification

| Test | Action | Expected Result | Pass? |
|------|--------|-----------------|-------|
| H35  | Subscribe to DI_00 with 200 ms sampling. Toggle DI_00. | Notification within 500 ms. | |
| H36  | Subscribe to AI_00 with 500 ms sampling. Change input voltage. | Notification within 1 s. | |
| H37  | Subscribe to all 36 variables simultaneously. | All 36 update within their sampling interval. No overflow. | |

### 2.6 Resource monitoring

Add these debug prints to the firmware (or read via a debug OPC UA
variable):

```c
/* In vOpcUaServerTask, every 60 seconds: */
UBaseType_t stackHW = uxTaskGetStackHighWaterMark(NULL);
size_t freeHeap = xPortGetFreeHeapSize();
size_t minHeap = xPortGetMinimumEverFreeHeapSize();
printf("Stack HW: %u, Free heap: %u, Min heap: %u\n",
       (unsigned)(stackHW * sizeof(StackType_t)),
       (unsigned)freeHeap, (unsigned)minHeap);
```

- [ ] **H38**: Stack high-water mark > 2048 bytes after 10 min of SCADA polling.
- [ ] **H39**: Free heap > 20 KB after 10 min.
- [ ] **H40**: Minimum-ever free heap > 15 KB after 1 hour.

---

## Phase 3: Stress Testing

**Goal**: Verify the server survives sustained load, network drops,
and concurrent Modbus + OPC UA traffic.

**Duration**: 2–3 days.

### 3.1 Sustained load

- [ ] **S1**: 3 concurrent SCADA clients (UaExpert on 3 PCs) polling all
      36 variables at 200 ms intervals for **24 hours**.  No crash,
      no memory leak (min-ever-free-heap stable after hour 1).
- [ ] **S2**: 1 client subscribing to all 36 variables with 200 ms
      sampling for **24 hours**.  Notification count ≈ 36 × 5 × 86400
      = 15,552,000.  No dropped notifications.

### 3.2 Network resilience

- [ ] **S3**: Unplug Ethernet for 60 seconds, reconnect.  Server
      recovers within 10 seconds.  All clients reconnect automatically.
- [ ] **S4**: Disconnect 1 client mid-subscription.  Server frees the
      subscription within 60 seconds (no leak).
- [ ] **S5**: Connect 10 clients simultaneously (exceeding
      `UA_MAXSESSIONCOUNT=4`).  Server rejects the excess clients
      with `BadTooManySessions` — does NOT crash.

### 3.3 Power cycling

- [ ] **S6**: 100 rapid power-on / power-off cycles (1 sec on, 1 sec
      off).  Server starts every time.  No flash corruption.
- [ ] **S7**: Power-fail during a client write to DO_03.  On reboot,
      DO_03 defaults to OFF (safe state).

### 3.4 Concurrency with Modbus

- [ ] **S8**: Run the existing Modbus task at full speed while 2 OPC UA
      clients poll all 36 variables.  No data corruption (DI/AI values
      consistent between Modbus and OPC UA).  No deadlocks.
- [ ] **S9**: Call `EmergencyStop` while Modbus is writing DOs.  All
      outputs go OFF within 50 ms regardless of Modbus activity.

### 3.5 Error injection

- [ ] **S10**: Send a malformed OPC UA message (random bytes on port
      4840).  Server returns an ERR message and closes the connection.
      Does NOT crash.
- [ ] **S11**: Send a write to a read-only node (DI_00).  Server
      returns `BadUserAccessDenied`.
- [ ] **S12**: Send a write with wrong data type (Int32 to a Boolean
      node).  Server returns `BadTypeMismatch`.

---

## Phase 4: Security Hardening

**Goal**: Bring the server to production-grade security.

**Duration**: 3–5 days.

### 4.1 Enable encryption

```bash
# Generate a self-signed device certificate
openssl req -x509 -newkey rsa:2048 -keyout device.key -out device.crt \
    -days 365 -nodes \
    -subj "/CN=STM32-OPC-UA-Gateway/O=Industrial/C=DE"
```

- [ ] **SEC1**: `UA_ENABLE_ENCRYPTION=1` in `ua_config.h`.
- [ ] **SEC2**: mbedTLS compiled into the STM32 firmware.
- [ ] **SEC3**: Device certificate and private key loaded into flash.
- [ ] **SEC4**: `UA_ServerConfig_setDefaultWithSecurityPolicies` used
      instead of `setMinimal`.
- [ ] **SEC5**: Client can connect with `Basic256Sha256` security policy.
- [ ] **SEC6**: `tcpdump` capture shows encrypted OPC UA traffic — no
      plaintext I/O values visible.

### 4.2 Enable access control

- [ ] **SEC7**: `OPCUA_ENABLE_ACCESS_CONTROL=1` in the build settings.
- [ ] **SEC8**: Default credentials changed from `engineer/opcua123` to
      production values stored in flash.
- [ ] **SEC9**: Anonymous login rejected (`BadIdentityTokenInvalid`).
- [ ] **SEC10**: Wrong password rejected after 3 attempts.
- [ ] **SEC11**: Read-only user can read DI/AI but cannot write DO/AO/Relay.

### 4.3 Network hardening

- [ ] **SEC12**: Firewall on the gateway restricts port 4840 to known
      SCADA IP addresses only.
- [ ] **SEC13**: No debug output (RTT/UART) in the release build.
- [ ] **SEC14**: Watchdog timeout set to 5 seconds — a deadlock resets
      the gateway.

---

## Phase 5: Factory Acceptance Test

**Goal**: Final validation with the customer's SCADA system.

**Duration**: 1–2 days.

- [ ] **FAT1**: Customer's SCADA connects to the gateway.
- [ ] **FAT2**: SCADA browses the address space and recognises all 36
      I/O variables.
- [ ] **FAT3**: SCADA reads DI_00 → value matches physical input.
- [ ] **FAT4**: SCADA writes DO_00 = true → physical output turns ON.
- [ ] **FAT5**: SCADA creates a subscription on AI_00 → values update
      in the SCADA trending view.
- [ ] **FAT6**: SCADA calls `EmergencyStop` → all outputs go OFF.
- [ ] **FAT7**: SCADA calls `ResetCounter` → returns `Good`.
- [ ] **FAT8**: SCADA maintains connection for 8 hours without
      disconnection or data loss.
- [ ] **FAT9**: Customer signs the acceptance form.

---

## Sign-off Checklist

| Phase | Tests | Engineer | Date | Pass? |
|-------|-------|----------|------|-------|
| Desktop | D1–D7 | | | |
| HIL | H1–H40 | | | |
| Stress | S1–S12 | | | |
| Security | SEC1–SEC14 | | | |
| FAT | FAT1–FAT9 | | | |

**All phases must pass before the gateway is deployed to a production
environment.**
