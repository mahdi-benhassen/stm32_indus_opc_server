"""
opcua_hello.py

Send an OPC UA Hello (HEL) message to 127.0.0.1:4840 and look for an
Acknowledge (ACK) reply.  Used by the CI smoke test to prove the
open62541 server is actually bound and answering.

The Python `opcua` package (opcua-asyncio) is the most reliable
implementation of the OPC UA Part 6 transport framing.
"""

import socket
import struct
import sys
import time


def build_hello() -> bytes:
    """Build a minimal OPC UA Hello message per Part 6, Section 7.1.2.3.

    Body fields, with non-zero sizes (the spec is unclear about
    whether 0 is permitted for maxMessageSize/maxChunkCount; many
    servers reject it as a limit violation):
        protocolVersion     = 0
        receiveBufferSize   = 65535
        sendBufferSize      = 65535
        maxMessageSize      = 16777216 (16 MB)
        maxChunkCount       = 64
        endpointUrlLength   = 0
        endpointUrl         = ""
    """
    endpoint = b""
    body = struct.pack("<IIIII", 0, 65535, 65535, 16777216, 64)
    body += struct.pack("<i", len(endpoint)) + endpoint

    msg_size = 8 + 8 + len(body)
    header = b"HEL" + b"F" + struct.pack("<I", msg_size) + struct.pack("<I", 0)
    seq = struct.pack("<II", 1, 1)
    return header + seq + body


def main() -> int:
    hello = build_hello()
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
            print("got from server: {!r} (first 8 bytes hex: {})".format(
                data[:32], data[:8].hex()))
            if data[:3] == b"ACK":
                print("OPC UA server on :4840 returned ACK after {}ms - OK".format(
                    (i + 1) * 200))
                return 0
        except (ConnectionRefusedError, socket.timeout, OSError) as e:
            print("connect attempt {}: {}".format(i + 1, e))
    print("OPC UA server did not respond with ACK on :4840 within 5s")
    return 1


if __name__ == "__main__":
    sys.exit(main())
