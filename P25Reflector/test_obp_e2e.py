#!/usr/bin/env python3
"""
test_obp_e2e.py — end-to-end smoke test for the P25Reflector OpenBridge link.

Brings up:
  * P25Reflector (built binary) with [OpenBridge] + [Vocoder] enabled
  * a fake OBP peer on 127.0.0.1:62031 (sends/receives signed DMRD)
  * a fake P25 repeater on an ephemeral port (polls + sends voice)
  * a vocoder emulator on 127.0.0.1:43000 (AMBE 7B<->320B) and :43001
    (IMBE 11B<->320B) — echo-style, validates the plumbing not the audio.

Checks:
  RX (OBP -> P25): VHEAD + 2 bursts + VTERM from the peer must make the
      reflector emit exactly: 0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x80
      to the linked repeater, with the right dst/src ids in 0x65/0x66.
  TX (P25 -> OBP): a local 0x64..0x69 + 0x80 call must make the reflector
      send DMRDs flagged 0x21 (VHEAD), 0x10 (VOICESYNC burst), 0x01 (burst
      pos 1), 0x22 (VTERM) with one common stream_id, incrementing seq, and
      all HMACs valid under the shared passphrase.
"""

import hmac
import hashlib
import os
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))

OBP_PASS = b"secret"
OBP_LOCAL_PORT = 42000      # reflector binds this; peer sends here
OBP_PEER_PORT = 62031       # peer listens; reflector sends here
P25_PORT = 41000            # reflector local P25 port
VOC_AMBE_PORT = 43000
VOC_IMBE_PORT = 43001

SRC_ID = 12345
DST_TG = 65537
NETID = 999999

failures = []

def check(cond, msg):
    if cond:
        print("PASS %s" % msg)
    else:
        print("FAIL %s" % msg)
        failures.append(msg)

# --------------------------------------------------------------------------
# DMRD build/verify (mirrors OpenBridge/obp.h)
# --------------------------------------------------------------------------

def dmr_sign(body, passphrase=OBP_PASS):
    assert len(body) == 53
    return body + hmac.new(passphrase, body, hashlib.sha1).digest()

def dmr_build(seq, src, dst, netid, flags, stream, payload33):
    body = bytearray(b"DMRD")
    body.append(seq & 0xFF)
    body += src.to_bytes(3, "big")
    body += dst.to_bytes(3, "big")
    body += netid.to_bytes(4, "big")
    body.append(flags)
    body += stream
    body += payload33
    assert len(body) == 53
    return dmr_sign(bytes(body))

def dmr_verify(pkt, passphrase=OBP_PASS):
    if len(pkt) != 73 or pkt[:4] != b"DMRD":
        return False
    body = pkt[:53]
    return hmac.new(passphrase, body, hashlib.sha1).digest() == pkt[53:]

# --------------------------------------------------------------------------
# vocoder emulator
# --------------------------------------------------------------------------

def vocoder_server(port, small_len):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", port))
    while True:
        try:
            data, addr = s.recvfrom(400)
        except OSError:
            return
        if len(data) == small_len:
            s.sendto(bytes(320), addr)          # decode -> silence PCM
        elif len(data) == 320:
            s.sendto(bytes(small_len), addr)    # encode -> zero unit
    # (thread dies when the process exits)

# --------------------------------------------------------------------------
# fake OBP peer
# --------------------------------------------------------------------------

class ObpPeer:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", OBP_PEER_PORT))
        self.sock.settimeout(0.2)
        self.rx = []

    def poll(self):
        try:
            while True:
                data, _ = self.sock.recvfrom(200)
                self.rx.append(data)
        except socket.timeout:
            pass

    def send(self, pkt):
        self.sock.sendto(pkt, ("127.0.0.1", OBP_LOCAL_PORT))

# --------------------------------------------------------------------------
# fake P25 repeater
# --------------------------------------------------------------------------

class P25Repeater:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.2)
        self.rx = []

    def send(self, data):
        self.sock.sendto(data, ("127.0.0.1", P25_PORT))

    def poll(self):
        try:
            while True:
                data, _ = self.sock.recvfrom(200)
                self.rx.append(data)
        except socket.timeout:
            pass

def imbe_frame(t, imbe):
    """Build a P25 wire voice frame of type t carrying the 11B IMBE word."""
    if t in (0x62, 0x6B):
        f = bytearray([0x62 if t == 0x62 else 0x6B, 2, 2, 0x0C, 0x0B, 0x12, 0x64, 0, 0, 0x80])
        f += bytes(12)
        f[10:21] = imbe
        return bytes(f)
    if t in (0x63, 0x6C):
        f = bytearray(14)
        f[0] = t
        f[1:12] = imbe
        f[13] = 2
        return bytes(f)
    if t in (0x6A, 0x73):
        f = bytearray([t, 0, 0, 2])
        f += imbe
        f += bytes(1)
        return bytes(f)
    # 17-byte types
    f = bytearray(17)
    f[0] = t
    prefix = {0x67: "F09D6A00", 0x68: "19D42600", 0x69: "E0EB7B00",
              0x71: "ACB8A400", 0x72: "9BDC7500"}.get(t)
    if prefix:
        f[1:5] = bytes.fromhex(prefix)
    f[5:16] = imbe
    f[16] = 2
    return bytes(f)

def main():
    # scratch dir so the log file does not litter the tree
    run_dir = os.path.join(HERE, "obp_e2e_run")
    os.makedirs(run_dir, exist_ok=True)
    if os.path.exists(os.path.join(HERE, "DMRIds.dat")):
        import shutil
        shutil.copy(os.path.join(HERE, "DMRIds.dat"), run_dir)

    with open(os.path.join(run_dir, "test.ini"), "w") as f:
        f.write("""[General]
Daemon=0

[Id Lookup]
Name=DMRIds.dat
Time=24

[Log]
DisplayLevel=1
FileLevel=1
FilePath=.
FileRoot=P25Reflector
FileRotate=1

[Network]
Port=%d
Debug=0

[OpenBridge]
Enabled=1
Address=127.0.0.1
PeerPort=%d
Port=%d
Passphrase="secret"
ID=%d
Callsign="P25TEST"
Debug=0

[Vocoder]
Enabled=1
Address=127.0.0.1
AmbePort=%d
ImbePort=%d
""" % (P25_PORT, OBP_PEER_PORT, OBP_LOCAL_PORT, NETID, VOC_AMBE_PORT, VOC_IMBE_PORT))

    voc_a = threading.Thread(target=vocoder_server, args=(VOC_AMBE_PORT, 7), daemon=True)
    voc_i = threading.Thread(target=vocoder_server, args=(VOC_IMBE_PORT, 11), daemon=True)
    voc_a.start()
    voc_i.start()

    peer = ObpPeer()
    rpt = P25Repeater()

    proc = subprocess.Popen([os.path.join(HERE, "P25Reflector"), "test.ini"],
                            cwd=run_dir,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)

    try:
        # link the fake repeater
        rpt.send(b"\xF0" + b"TESTRPT".ljust(10, b" "))
        time.sleep(0.3)
        rpt.poll()
        check(any(d[0] == 0xF0 for d in rpt.rx), "repeater poll echoed back")

        # ---------------- RX: OBP -> P25 ----------------
        stream = bytes([0xAB, 0xCD, 0xEF, 0x01])
        payload = bytes(33)
        peer.send(dmr_build(1, SRC_ID, DST_TG, NETID, 0x21, stream, payload))   # VHEAD
        time.sleep(0.05)
        peer.send(dmr_build(2, SRC_ID, DST_TG, NETID, 0x10, stream, payload))   # burst pos0
        peer.send(dmr_build(3, SRC_ID, DST_TG, NETID, 0x01, stream, payload))   # burst pos1
        time.sleep(0.05)
        peer.send(dmr_build(4, SRC_ID, DST_TG, NETID, 0x22, stream, payload))   # VTERM

        deadline = time.time() + 3.0
        while time.time() < deadline and not any(d[0] == 0x80 for d in rpt.rx):
            rpt.poll()
            time.sleep(0.05)
        rpt.poll()

        types = [d[0] for d in rpt.rx if d[0] not in (0xF0,)]
        expected = [0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x80]
        check(types == expected,
              "RX frame sequence is %s (want %s)" % (
                  [hex(t) for t in types], [hex(t) for t in expected]))

        f65 = next((d for d in rpt.rx if d[0] == 0x65), None)
        f66 = next((d for d in rpt.rx if d[0] == 0x66), None)
        check(f65 is not None and f65[1:4] == DST_TG.to_bytes(3, "big"),
              "0x65 carries TG %d" % DST_TG)
        check(f66 is not None and f66[1:4] == SRC_ID.to_bytes(3, "big"),
              "0x66 carries src %d" % SRC_ID)

        # ---------------- TX: P25 -> OBP ----------------
        rpt.rx.clear()
        peer.rx.clear()
        imbe = bytes([0x04, 0x0C, 0xFD, 0x7B, 0xFB, 0x7D, 0xF2, 0x7B, 0x3D, 0x9E, 0x44])

        def send_local(frame):
            rpt.send(frame)
            time.sleep(0.03)

        f64 = bytearray(17); f64[0] = 0x64; f64[5:16] = imbe; f64[16] = 2
        send_local(bytes(f64))                                   # lcf=0 group
        f65b = bytearray(17); f65b[0] = 0x65
        f65b[1:4] = (9999).to_bytes(3, "big")
        f65b[5:16] = imbe; f65b[16] = 2
        send_local(bytes(f65b))                                  # dst 9999
        f66b = bytearray(17); f66b[0] = 0x66
        f66b[1:4] = (54321).to_bytes(3, "big")
        f66b[5:16] = imbe; f66b[16] = 2
        send_local(bytes(f66b))                                  # src 54321 -> VHEAD
        send_local(imbe_frame(0x67, imbe))
        send_local(imbe_frame(0x68, imbe))
        send_local(imbe_frame(0x69, imbe))                       # -> burst pos1
        f80 = bytearray(17); f80[0] = 0x80
        send_local(bytes(f80))                                          # -> VTERM

        deadline = time.time() + 2.0
        while time.time() < deadline and not any(
                d[15] == 0x22 for d in peer.rx if len(d) == 73):
            peer.poll()
            time.sleep(0.05)
        peer.poll()

        pkts = [d for d in peer.rx if len(d) == 73]
        check(all(dmr_verify(d) for d in pkts), "all TX DMRDs HMAC-verify")

        flags = [d[15] for d in pkts]
        want = [0x21, 0x10, 0x01, 0x22]
        check(flags == want, "TX flag sequence is %s (want %s)" % (
            [hex(x) for x in flags], [hex(x) for x in want]))

        if pkts:
            streams = {d[16:20] for d in pkts}
            check(len(streams) == 1, "one common stream_id across the call")
            seqs = [d[4] for d in pkts]
            check(seqs == list(range(seqs[0], seqs[0] + len(seqs))),
                  "TX seq increments: %s" % seqs)
            dst0 = int.from_bytes(pkts[0][8:11], "big")
            src0 = int.from_bytes(pkts[0][5:8], "big")
            check(dst0 == 9999 and src0 == 54321,
                  "TX ids src=54321 dst=9999 (got %d/%d)" % (src0, dst0))

        time.sleep(0.5)
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    print()
    if failures:
        print("FAILED: %d check(s)" % len(failures))
        return 1
    print("ALL CHECKS PASSED")
    return 0

if __name__ == "__main__":
    sys.exit(main())
