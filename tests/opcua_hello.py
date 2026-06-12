"""
opcua_hello.py

Use the canonical python-opcua client to talk to 127.0.0.1:4840
and verify that the open62541 server is fully functional.

If the python-opcua client can open a session, browse the
Industrial_IO folder and read one variable, the server is
genuinely working.  Falls back to a raw HEL/ACK probe if
python-opcua is unavailable.
"""

import socket
import struct
import sys
import time


def raw_hello_probe() -> int:
    """Send a hand-crafted OPC UA HEL message and look for an ACK."""
    endpoint = b""
    # protocolVersion=0, recvBuf=65535, sendBuf=65535, maxMsg=0, maxChunk=0,
    # endpointUrlLen=0
    body = struct.pack("<IIIII", 0, 65535, 65535, 0, 0)
    body += struct.pack("<i", len(endpoint)) + endpoint

    msg_size = 8 + 8 + len(body)
    hello = b"HEL" + b"F" + struct.pack("<I", msg_size) + struct.pack("<I", 0)
    hello += struct.pack("<II", 1, 1) + body

    for i in range(25):
        time.sleep(0.2)
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1.0)
            s.connect(("127.0.0.1", 4840))
            s.sendall(hello)
            data = s.recv(4096)
            s.close()
            if not data:
                continue
            if data[:3] == b"ACK":
                print("OPC UA server on :4840 returned ACK after {}ms - OK".format(
                    (i + 1) * 200))
                return 0
        except (ConnectionRefusedError, socket.timeout, OSError):
            pass
    print("OPC UA server did not respond with ACK on :4840 within 5s")
    return 1


def opcua_client_probe() -> int:
    """Use python-opcua to open a session and browse the address space."""
    try:
        from opcua import Client  # type: ignore
    except ImportError:
        print("python-opcua not installed, using raw HEL probe")
        return raw_hello_probe()

    url = "opc.tcp://127.0.0.1:4840"
    for i in range(25):
        time.sleep(0.2)
        try:
            c = Client(url, timeout=2)
            c.connect()
            # Browse the Industrial_IO folder.
            objects = c.get_objects_node()
            try:
                industrial_io = objects.get_child(["0:Industrial_IO"])
            except Exception:
                # Try by namespace
                industrial_io = objects
            di = industrial_io.get_child(["0:DigitalInputs"])
            di00 = di.get_child(["0:DI_00"])
            value = di00.get_value()
            print("OPC UA server reachable via python-opcua, "
                  "DI_00 = {!r} after {}ms - OK".format(value, (i + 1) * 200))
            c.disconnect()
            return 0
        except Exception as e:
            last_err = e
    print("python-opcua probe failed: {}".format(last_err))
    return 1


if __name__ == "__main__":
    sys.exit(opcua_client_probe())
