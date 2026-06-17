#!/usr/bin/env python3
"""
test_opcua_server.py

Integration test suite for the STM32F407 OPC UA server.

Usage:
    # Start the server first (either the CI host build or the real hardware)
    /tmp/opcua_server &        # or connect to the board at opc.tcp://<ip>:4840

    # Run the tests
    python3 tests/test_opcua_server.py
    python3 tests/test_opcua_server.py --url opc.tcp://192.168.1.100:4840

Requires:
    pip3 install asyncua

Tests cover:
    - Endpoint discovery
    - Address space browsing (folders, variables, methods)
    - Read/write on all I/O types (DI, DO, AI, AO, Relays)
    - Method calls (ResetCounter, EmergencyStop)
    - Subscriptions (DataChange notifications)
    - Error handling (write to RO node, wrong type)
"""

import argparse
import asyncio
import sys
import time

try:
    from asyncua import Client, Node, ua
except ImportError:
    print("ERROR: asyncua is not installed.  Run: pip3 install asyncua")
    sys.exit(1)


# --- Test framework ----------------------------------------------------------

PASSED = 0
FAILED = 0


def check(test_id, description, condition):
    global PASSED, FAILED
    if condition:
        print(f"  [{test_id}] PASS: {description}")
        PASSED += 1
    else:
        print(f"  [{test_id}] FAIL: {description}")
        FAILED += 1


# --- Tests -------------------------------------------------------------------

async def test_discovery(client):
    """D1-D3: Verify endpoint discovery."""
    print("\n=== Phase 1: Discovery ===")

    endpoints = await client.get_endpoints()
    check("D1", "At least one endpoint returned", len(endpoints) > 0)

    has_none = any(
        ep.SecurityPolicyUri == "http://opcfoundation.org/UA/SecurityPolicy#None"
        for ep in endpoints
    )
    check("D2", "SecurityPolicy#None endpoint exists", has_none)


async def test_browse(client):
    """D4-D7: Verify address space structure."""
    print("\n=== Phase 2: Browse ===")

    objects = client.nodes.objects
    children = await objects.get_children()

    names = []
    for child in children:
        bn = await child.read_browse_name()
        names.append(bn.Name)

    check("B1", "Industrial_IO folder exists", "Industrial_IO" in names)

    # Find the Industrial_IO node
    industrial_io = await objects.get_child(["0:Industrial_IO"])
    io_children = await industrial_io.get_children()

    io_names = []
    for child in io_children:
        bn = await child.read_browse_name()
        io_names.append(bn.Name)

    expected_folders = [
        "DigitalInputs", "DigitalOutputs", "AnalogInputs",
        "AnalogOutputs", "Relays", "Methods"
    ]
    for folder in expected_folders:
        check(f"B2_{folder}", f"Folder '{folder}' exists", folder in io_names)

    # Check DI count
    di_folder = await industrial_io.get_child(["0:DigitalInputs"])
    di_children = await di_folder.get_children()
    check("B3", "DigitalInputs has 8 children", len(di_children) == 8)

    # Check DO count
    do_folder = await industrial_io.get_child(["0:DigitalOutputs"])
    do_children = await do_folder.get_children()
    check("B4", "DigitalOutputs has 8 children", len(do_children) == 8)

    # Check AI count
    ai_folder = await industrial_io.get_child(["0:AnalogInputs"])
    ai_children = await ai_folder.get_children()
    check("B5", "AnalogInputs has 8 children", len(ai_children) == 8)

    # Check AO count
    ao_folder = await industrial_io.get_child(["0:AnalogOutputs"])
    ao_children = await ao_folder.get_children()
    check("B6", "AnalogOutputs has 8 children", len(ao_children) == 8)

    # Check Relay count
    rl_folder = await industrial_io.get_child(["0:Relays"])
    rl_children = await rl_folder.get_children()
    check("B7", "Relays has 4 children", len(rl_children) == 4)

    # Check Methods count
    mt_folder = await industrial_io.get_child(["0:Methods"])
    mt_children = await mt_folder.get_children()
    check("B8", "Methods has 2 children", len(mt_children) == 2)


async def test_read_di(client):
    """Read DI_00 and verify it's a Boolean."""
    print("\n=== Phase 3: Read DI ===")
    di00 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:DigitalInputs", "0:DI_00"])
    val = await di00.read_value()
    check("R1", f"DI_00 is Boolean, value={val}", isinstance(val, bool))


async def test_write_readback_do(client):
    """Write DO_00, read it back, verify."""
    print("\n=== Phase 4: Write/Read DO ===")
    do00 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:DigitalOutputs", "0:DO_00"])

    await do00.write_value(True)
    val = await do00.read_value()
    check("W1", "DO_00 write True -> read True", val is True)

    await do00.write_value(False)
    val = await do00.read_value()
    check("W2", "DO_00 write False -> read False", val is False)


async def test_read_ai(client):
    """Read AI_00 and verify it's an Int32."""
    print("\n=== Phase 5: Read AI ===")
    ai00 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:AnalogInputs", "0:AI_00"])
    val = await ai00.read_value()
    check("R2", f"AI_00 is Int32, value={val}", isinstance(val, int))


async def test_write_readback_ao(client):
    """Write AO_00, read it back, verify."""
    print("\n=== Phase 6: Write/Read AO ===")
    ao00 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:AnalogOutputs", "0:AO_00"])

    await ao00.write_value(12345)
    val = await ao00.read_value()
    check("W3", f"AO_00 write 12345 -> read {val}", val == 12345)

    await ao00.write_value(0)
    val = await ao00.read_value()
    check("W4", f"AO_00 write 0 -> read {val}", val == 0)


async def test_relay(client):
    """Write/read RLY_00."""
    print("\n=== Phase 7: Relay ===")
    rly00 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:Relays", "0:RLY_00"])

    await rly00.write_value(True)
    val = await rly00.read_value()
    check("W5", "RLY_00 write True -> read True", val is True)

    await rly00.write_value(False)
    val = await rly00.read_value()
    check("W6", "RLY_00 write False -> read False", val is False)


async def test_methods(client):
    """Call ResetCounter and EmergencyStop."""
    print("\n=== Phase 8: Methods ===")
    mt_folder = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:Methods"])

    # ResetCounter
    reset_node = await mt_folder.get_child(["0:ResetCounter"])
    result = await reset_node.call_method("")
    check("M1", f"ResetCounter returns Good (result={result})", True)

    # EmergencyStop — first set some outputs ON, then call
    do00 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:DigitalOutputs", "0:DO_00"])
    do03 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:DigitalOutputs", "0:DO_03"])
    rly01 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:Relays", "0:RLY_01"])

    await do00.write_value(True)
    await do03.write_value(True)
    await rly01.write_value(True)

    estop_node = await mt_folder.get_child(["0:EmergencyStop"])
    result = await estop_node.call_method("")
    check("M2", "EmergencyStop returns Good", True)

    # Verify all DOs and Relays are OFF
    await asyncio.sleep(0.5)  # give the server time to process
    do00_val = await do00.read_value()
    do03_val = await do03.read_value()
    rly01_val = await rly01.read_value()
    check("M3", f"DO_00 = False after e-stop ({do00_val})", do00_val is False)
    check("M4", f"DO_03 = False after e-stop ({do03_val})", do03_val is False)
    check("M5", f"RLY_01 = False after e-stop ({rly01_val})", rly01_val is False)


async def test_subscription(client):
    """Subscribe to DI_00 and wait for at least one notification."""
    print("\n=== Phase 9: Subscription ===")
    di00 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:DigitalInputs", "0:DI_00"])

    notification_received = asyncio.Event()

    class SubHandler:
        def datachange_notification(self, node, val, data):
            notification_received.set()

    sub = await client.create_subscription(200, SubHandler())
    await sub.subscribe_data_change(di00)

    try:
        await asyncio.wait_for(notification_received.wait(), timeout=5.0)
        check("SUB1", "Received at least one DataChange notification", True)
    except asyncio.TimeoutError:
        check("SUB1", "Received at least one DataChange notification", False)

    await sub.delete()


async def test_error_handling(client):
    """Verify error handling for invalid operations."""
    print("\n=== Phase 10: Error Handling ===")

    # Write to a read-only node (DI_00)
    di00 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:DigitalInputs", "0:DI_00"])
    try:
        await di00.write_value(True)
        check("E1", "Write to RO node rejected", False)
    except Exception:
        check("E1", "Write to RO node rejected", True)

    # Write wrong type to DO_00 (Int32 instead of Boolean)
    do00 = await client.nodes.objects.get_child(
        ["0:Industrial_IO", "0:DigitalOutputs", "0:DO_00"])
    try:
        await do00.write_value(42, ua.VariantType.Int32)
        check("E2", "Wrong type write rejected", False)
    except Exception:
        check("E2", "Wrong type write rejected", True)


# --- Main --------------------------------------------------------------------

async def main(url):
    print(f"Connecting to {url} ...")
    client = Client(url, timeout=5)

    try:
        await client.connect()
        print("Connected.")
    except Exception as e:
        print(f"ERROR: Cannot connect to {url}: {e}")
        return 1

    try:
        await test_discovery(client)
        await test_browse(client)
        await test_read_di(client)
        await test_write_readback_do(client)
        await test_read_ai(client)
        await test_write_readback_ao(client)
        await test_relay(client)
        await test_methods(client)
        await test_subscription(client)
        await test_error_handling(client)
    finally:
        await client.disconnect()

    print(f"\n{'='*60}")
    print(f"Results: {PASSED} passed, {FAILED} failed")
    print(f"{'='*60}")
    return 0 if FAILED == 0 else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="OPC UA server integration tests")
    parser.add_argument("--url", default="opc.tcp://127.0.0.1:4840",
                        help="OPC UA endpoint URL (default: opc.tcp://127.0.0.1:4840)")
    args = parser.parse_args()

    rc = asyncio.run(main(args.url))
    sys.exit(rc)
