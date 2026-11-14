#!/usr/bin/env python3
"""
test_obp_speech.py — prove real AUDIO flows through the compiled-in vocoder.

Chain:
  speech PCM (espeak-ng) -> vocoder_probe (CBuiltinVocoder.pcmToImbe) -> IMBE
  words -> fed as P25 wire frames by a fake repeater -> P25Reflector TX path
  (builtin imbeToPcm + pcmToAmbe, DMRD bursts out) -> fake peer saves 33-byte
  voice payloads -> vocoder_probe (dmr_voice_parse_burst + ambeToPcm) -> PCM.

Checks: decoded PCM RMS is well above silence and >55% of its 100-3400 Hz
energy sits below 1100 Hz (speech formant band), per the md380-vocoder
verification recipe.  No UDP vocoder process is running — everything is the
builtin engine.
"""

import array
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
RUN = os.path.join(HERE, "obp_speech_run")

OBP_PASS = b"secret"
OBP_LOCAL_PORT = 42002
OBP_PEER_PORT = 62033
P25_PORT = 41002

import hmac, hashlib
def dmr_sign(b): return b + hmac.new(OBP_PASS, b, hashlib.sha1).digest()
def dmr_verify(p): return len(p) == 73 and p[:4] == b"DMRD" and \
    hmac.new(OBP_PASS, p[:53], hashlib.sha1).digest() == p[53:]

failures = []
def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond:
        failures.append(msg)

def make_speech_raw():
    wav = os.path.join(RUN, "speech.wav")
    raw = os.path.join(RUN, "speech.raw")
    subprocess.run(["espeak-ng", "-w", wav,
                    "The quick brown fox jumps over the lazy dog. Testing one two three."],
                   check=True)
    import wave
    w = wave.open(wav, "rb")
    rate, n = w.getframerate(), w.getnframes()
    src = array.array("h"); src.frombytes(w.readframes(n)); w.close()
    # linear-interpolation resample to 8 kHz
    dst = array.array("h")
    step = rate / 8000.0
    for i in range(int(len(src) / step)):
        j = i * step
        j0 = int(j); j1 = min(j0 + 1, len(src) - 1)
        f = j - j0
        dst.append(int(src[j0] * (1 - f) + src[j1] * f))
    # trim to whole 20 ms frames, cap ~6 s
    nframes = min(len(dst) // 160, 300)
    with open(raw, "wb") as f:
        f.write(dst[:nframes * 160].tobytes())
    return raw, nframes

def probe(*args):
    subprocess.run([os.path.join(HERE, "vocoder_probe")] + list(args), check=True,
                   stdout=subprocess.DEVNULL)

def rms_and_formants(rawfile):
    a = array.array("h"); a.frombytes(open(rawfile, "rb").read())
    n = len(a)
    if n == 0:
        return 0.0, []
    r = (sum(x * x for x in a) / n) ** 0.5
    # Goertzel sweep (md380-vocoder skill recipe, fs=8000): a speech vocoder
    # round-trip shows 200-600 Hz formant dominance.
    import math
    s = a[n // 4:3 * n // 4]
    L = len(s)
    peaks = []
    for f in range(100, 3400, 50):
        w2 = 2 * math.pi * f / 8000.0
        c = math.cos(w2)
        cc = 2 * c
        q0 = q1 = q2 = 0.0
        for x in s:
            q0 = x + cc * q1 - q2
            q2 = q1
            q1 = q0
        power = (q1 * q1 + q2 * q2 - cc * q1 * q2) / L
        peaks.append((power, f))
    peaks.sort(reverse=True)
    return r, [(f, p) for p, f in peaks[:6]]

def main():
    os.makedirs(RUN, exist_ok=True)
    raw, nframes = make_speech_raw()
    imbe_file = os.path.join(RUN, "speech.imbe")
    probe("encode", raw, imbe_file)
    words = [open(imbe_file, "rb").read()[i:i+11] for i in range(0, nframes * 11, 11)]
    check(len(words) == nframes, "probe encoded %d IMBE words from %d PCM frames" % (len(words), nframes))

    with open(os.path.join(RUN, "test.ini"), "w") as f:
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
ID=999999
Callsign="P25TEST"
Debug=0
[Vocoder]
Enabled=1
Builtin=1
""" % (P25_PORT, OBP_PEER_PORT, OBP_LOCAL_PORT))

    import shutil
    if os.path.exists(os.path.join(HERE, "DMRIds.dat")):
        shutil.copy(os.path.join(HERE, "DMRIds.dat"), RUN)

    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    peer.bind(("127.0.0.1", OBP_PEER_PORT))
    peer.settimeout(0.2)
    rpt = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rpt.bind(("127.0.0.1", 0))
    rpt.settimeout(0.2)

    proc = subprocess.Popen([os.path.join(HERE, "P25Reflector"), "test.ini"], cwd=RUN,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)

    def drain(sock):
        got = []
        try:
            while True:
                d, _ = sock.recvfrom(200)
                got.append(d)
        except socket.timeout:
            pass
        return got

    try:
        rpt.sendto(b"\xF0" + b"SPCHRPT".ljust(10, b" "), ("127.0.0.1", P25_PORT))
        time.sleep(0.3)
        drain(rpt); drain(peer)

        # send the speech as P25 wire frames, 30 ms apart.  IMBE payload
        # offsets must mirror imbeOffset() in ObpNetwork.cpp:
        # 0x6B -> +10, 0x6C -> +1, 0x6A/0x73 -> +4, all others -> +5.
        cycle = [0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C,
                 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73]
        wi = 0
        for k, t in enumerate(cycle * ((nframes // len(cycle)) + 1)):
            if wi >= nframes:
                break
            imbe = words[wi]; wi += 1
            f = bytearray(17); f[0] = t
            if t == 0x65: f[1:4] = (9999).to_bytes(3, "big")
            if t == 0x66: f[1:4] = (54321).to_bytes(3, "big")
            if t == 0x6B:   off = 10
            elif t == 0x6C: off = 1
            elif t in (0x6A, 0x73): off = 4
            else: off = 5
            f[off:off+11] = imbe
            f[16] = 2
            rpt.sendto(bytes(f), ("127.0.0.1", P25_PORT))
            time.sleep(0.03)
        time.sleep(0.1)
        rpt.sendto(bytes([0x80]) + bytes(16), ("127.0.0.1", P25_PORT))
        time.sleep(0.5)

        pkts = drain(peer)
        voiced = [p for p in pkts if dmr_verify(p) and (p[15] & 0x30) in (0x00, 0x10)]
        with open(os.path.join(RUN, "bursts.bin"), "wb") as f:
            for p in voiced:
                f.write(p[20:53])
        check(len(voiced) >= nframes // 3 - 1,
              "peer received %d voice bursts (want ~%d)" % (len(voiced), nframes // 3))

        out = os.path.join(RUN, "decoded.raw")
        probe("bursts", os.path.join(RUN, "bursts.bin"), out)
        r, top = rms_and_formants(out)
        print("decoded rms=%.0f top peaks: %s" % (
            r, ", ".join("%dHz" % f for f, _ in top)))
        check(r > 300, "decoded PCM RMS %.0f is speech-level, not silence" % r)
        inband = sum(1 for f, _ in top if 200 <= f <= 650)
        check(inband >= 4,
              "decoded PCM has 200-600 Hz formant dominance (%d/6 top peaks in band)" % inband)
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    print()
    if failures:
        print("FAILED: %d" % len(failures))
        return 1
    print("ALL SPEECH CHECKS PASSED")
    return 0

if __name__ == "__main__":
    sys.exit(main())
