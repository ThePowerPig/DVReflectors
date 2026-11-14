#!/usr/bin/env python3
"""Interop cross-check: our C OpenBridge lib vs the HBlink4 reference (Python).

1. C signs a 73-byte DMRD packet -> Python verifies HMAC + fields.
2. Python builds + signs a 73-byte DMRD packet -> C verifies HMAC.
3. Python corrupts fields -> C must reject (-1 magic / -2 hmac).
"""
import hmac
import hashlib
import subprocess
import sys

PASS = "interop test passphrase 42"
XCHECK = "./xcheck"
LIB = sys.argv[1] if len(sys.argv) > 1 else "."

def c_sign():
    out = subprocess.run([XCHECK, "sign", PASS], capture_output=True, text=True, check=True)
    return bytes.fromhex(out.stdout.strip())

def c_verify(pkt: bytes) -> int:
    out = subprocess.run([XCHECK, "verify", PASS, pkt.hex()],
                         capture_output=True, text=True)
    return 1 if out.stdout.strip() == "VERIFY-OK" else 0

def build_dmrd(seq: int, src: int, dst: int, netid: int, flags: int,
               stream: bytes, payload: bytes) -> bytes:
    assert len(payload) == 33
    body = b"DMRD" + bytes([seq]) + src.to_bytes(3, "big") + dst.to_bytes(3, "big")
    body += netid.to_bytes(4, "big") + bytes([flags]) + stream + payload
    return body + hmac.new(PASS.encode(), body, hashlib.sha1).digest()

fails = 0
def check(cond, msg):
    global fails
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1

# 1. C -> Python
pkt = c_sign()
check(len(pkt) == 73, "C packet is 73 bytes")
check(pkt[:4] == b"DMRD", "C packet magic")
check(pkt[4] == 0x42, "C packet seq 0x42")
check(pkt[5:8] == bytes.fromhex("001234"), "C packet src 0x001234")
check(pkt[8:11] == bytes.fromhex("2c0001"), "C packet dst 0x2C0001")
check(pkt[11:15] == bytes.fromhex("0000abcd"), "C packet netid 0x0000abcd")
check(pkt[15] == 0x21, "C packet flags: DATASYNC|VHEAD (0x20|0x01)")
check(pkt[16:20] == bytes.fromhex("c0ffee00"), "C packet stream_id")
mac = hmac.new(PASS.encode(), pkt[:53], hashlib.sha1).digest()
check(mac == pkt[53:], "Python HMAC verifies C packet (the interop check)")

# 2. Python -> C
payload = bytes(range(33))
p2 = build_dmrd(0x77, 0x00AABB, 0x2C0002, 0xDEADBEEF, 0x03, bytes.fromhex("11223344"), payload)
check(c_verify(p2), "C verifies Python-signed packet")

# 3. corruption cases
bad = bytearray(p2); bad[20] ^= 0xFF
check(not c_verify(bytes(bad)), "C rejects Python packet with corrupted body")
bad = bytearray(p2); bad[0] = ord("X")
check(not c_verify(bytes(bad)), "C rejects Python packet with bad magic")

print(f"\ninterop: {fails} failures")
sys.exit(1 if fails else 0)
