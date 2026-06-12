"""
opcua_hello.py

Send an OPC UA Hello (HEL) message to 127.0.0.1:4840 and look for an
Acknowledge (ACK) reply.  Used by the CI smoke test to prove the
open62541 server is actually bound and answering.

OPC UA Part 6 transport framing:
  HELF (Hello + Final chunk):
    type        : 3 bytes  = "HEL"
    chunk type  : 1 byte   = 'F' (0x46)
    size        : uint32
    secureChanId: uint32   = 0
    tokenId     : uint32   = 0  (only present in Hello tokens, omitted here)
    seqNum      : uint32   = 0
    requestId   : uint32   = 0
    body:
      protocolVersion : uint32 = 0
      receiveBufferSize: uint32 = 0
      sendBufferSize   : uint32 = 0
      maxMessageSize   : uint32 = 0
      maxChunkCount    : uint32 = 0
      endpointUrlLen   : uint32
      endpointUrl      : bytes
"""

import socket
import struct
import sys
import time


def build_hello() -> bytes:
    endpoint = b"opc.tcp://127.0.0.1:4840"
    # Body: 5 x uint32 + 1 x uint32 (len) + endpoint
    body = struct.pack("<IIIII", 0, 0, 0, 0, 0)
    body += struct.pack("<I", len(endpoint)) + endpoint
    # Header: 4 (type+chunk) + 4 (size) + 4 (secChanId) + 4 (tokenId) +
    #         4 (seqNum) + 4 (requestId) = 24 bytes before body
    header = b"HEL\x46" + struct.pack("<I", 24 + len(body))
    return header + struct.pack("<IIIII", 0, 0, 0, 0, 0) + body


def main() -> int:
    hello = build_hello()
    for i in range(25):
        time.sleep(0.2)
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1.0)
            s.connect(("127.0.0.1", 4840))
            s.sendall(hello)
            data = s.recv(1024)
            s.close()
            if data[:3] == b"ACK":
                print("OPC UA server on :4840 returned ACK after {}ms - OK".format(
                    (i + 1) * 200))
                return 0
        except (ConnectionRefusedError, socket.timeout, OSError):
            pass
    print("OPC UA server did not respond on :4840 within 5s")
    return 1


if __name__ == "__main__":
    sys.exit(main())
