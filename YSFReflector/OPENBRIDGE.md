# OpenBridge on YSFReflector

YSFReflector can link directly to one HBlink3 or FreeDMR master (or any other OpenBridge peer) and bridge a single talkgroup. YSF DN audio and DMR both carry AMBE+2, so the reflector repacks the voice frames. It does not transcode them and it does not need a separate YSF2DMR process.

With `Enable=0`, or with the `[OpenBridge]` section omitted, the reflector behaves as before.

## What it does

- **YSF to DMR.** DN (V/D mode 2) from a linked YSF client is converted and sent to the peer on the configured talkgroup, timeslot 1, as a group call. The source DMR ID comes from the lookup file. The callsign is read from the YSFD network header.
- **DMR to YSF.** Group-call traffic from the peer on that talkgroup is converted to YSF DN and sent to every linked YSF client. The YSF source callsign comes from the lookup file, or from the decimal DMR ID when the ID is unknown. The destination callsign is `ALL`.
- **Stream edges.** A YSF header starts a DMR voice LC header (sent three times). A YSF terminator, or the end-of-transmission bit in the YSFD packet, finishes the DMR superframe and sends a terminator with LC. The reverse path does the same. If either side goes quiet for 1.5 seconds the bridge sends the missing terminator.
- **Echo.** The bridge remembers the stream IDs it transmits and drops them if the peer sends them back. While a YSF user is being bridged, incoming DMR on that talkgroup is ignored, and while DMR audio is being played to YSF clients it is not sent back to the peer. YSF-to-YSF fan-out is unchanged.

OpenBridge itself has no login. Each datagram is a 53-byte Homebrew `DMRD` packet plus a 20-byte HMAC-SHA1 of those 53 bytes, keyed with the shared passphrase. Bytes 11-14 carry this reflector's network ID. The source address of an accepted packet must be the configured peer address and port, which is how HBlink3 identifies the link.

## Configuration

```
[OpenBridge]
Enable=1
Address=192.0.2.10
Port=62035
LocalPort=62036
NetworkId=310999
Passphrase="a long shared secret"
Talkgroup=9999
IdFile=/var/lib/mmdvm/DMRIds.dat
FallbackId=3109990
ColorCode=1
Callsign=YSF-BRIDGE
Debug=0
```

| Key | Meaning |
| --- | --- |
| `Enable` | `1` to link, `0` to leave the reflector YSF-only. |
| `Address`, `Port` | Peer OpenBridge bind address and port. |
| `LocalPort` | UDP port this reflector binds. The peer's target port must be this value. On one host it must differ from `Port`. |
| `NetworkId` | 32-bit ID written into every DMRD packet. Use a different value from the peer. |
| `Passphrase` | HMAC-SHA1 key, sent as the raw text. Quote it if it contains spaces or `#`. |
| `Talkgroup` | Only this group call is bridged, both ways. |
| `IdFile` | Optional. `id callsign` (DMRIds.dat) or `id,callsign,...` (RadioID CSV). The first ID for a callsign is the one used on DMR. |
| `FallbackId` | DMR ID used when the YSF callsign is not in the file. `0` drops that transmission instead of inventing an ID. |
| `ColorCode` | Color code written into the DMR slot type and EMB. Default `1`. |
| `Callsign` | Up to 10 characters placed in the YSFD gateway field for audio that came from DMR. Default is `[Info] Name`. |
| `Debug` | `1` dumps OpenBridge packets at log level 1. |

The lookup file is read once at startup. A missing file is logged and the bridge still runs, using `FallbackId` for every YSF callsign.

## HBlink3

Give each side its own network ID and point each target at the other side's bind port. The passphrase must match. HBlink only accepts packets whose source address is the configured target, so `TARGET_PORT` has to be the port the reflector actually sends from (`LocalPort`).

```
[OBP-YSF]
MODE: OPENBRIDGE
ENABLED: True
IP:
PORT: 62035
NETWORK_ID: 310998
PASSPHRASE: a long shared secret
TARGET_IP: 192.0.2.20
TARGET_PORT: 62036
BOTH_SLOTS: False
USE_ACL: True
SUB_ACL: PERMIT:ALL
TG1_ACL: PERMIT:9999
```

Matching reflector settings: `Address` / `Port` are the master's `IP` / `PORT`, `LocalPort` is `62036`, `NetworkId` is `310999` (not `310998`), `Talkgroup` is `9999`.

On the HBlink side, bridge that OpenBridge system to a master or repeater on slot 1 talkgroup 9999. Hotspots should use slot 1. `BOTH_SLOTS` stays off; this build always transmits and accepts slot 1 only.

FreeDMR uses the same OpenBridge framing. Use its OpenBridge peer fields the same way: shared passphrase, distinct network IDs, and target port equal to `LocalPort`.

## Trying it without a radio

From `YSFReflector/`:

```
make test
```

`OpenBridgeTest` checks HMAC-SHA1 against the RFC 2202 vectors, DMR ID lookup, ini parsing, YSF FICH and DMR full-LC encode/decode, an AMBE round trip through the YSF2DMR converter, and a localhost OpenBridge loopback (YSF DN in one side, YSF DN out the other, including end of transmission, echo suppression, a wrong talkgroup, a bad passphrase, the fallback ID, and the silence watchdog).

A reflector with the stock ini (`Enable=0`) still starts as a normal YSF reflector. To see the link come up, set `Enable=1`, point it at a second process or at HBlink, and confirm the log line `OpenBridge listening on ...`.

## On-air testing still to do

This has been exercised with the loopback harness, not with a radio. Before relying on it, check all of the following on a real hotspot and a YSF radio in DN:

- The DMR radio unmutes, shows the mapped talkgroup and ID, and the audio is intelligible.
- The YSF radio unmutes, shows a sensible callsign, and returns to hang time when the DMR user unkeys.
- A YSF transmission ends cleanly on DMR (no stuck talkgroup) both on unkey and if the YSF client disappears.
- The master does not play the bridge back to itself. If it does, the stream-ID filter should drop it; confirm there is no howl or doubled audio.
- `ColorCode` matches what the hotspot expects if a radio stays muted.
- Two people keying at once: the bridge is half duplex, so the second direction is dropped until the first transmission ends. YSF clients still hear each other.

## Limitations

- Only YSF DN (V/D mode 2) is converted. Voice Wide (VW / voice FR), data FR, and V/D mode 1 are repeated to other YSF clients and are not sent to DMR.
- One peer, one talkgroup, slot 1, group calls. Private calls, slot 2, GPS, Wires-X, and DTMF talkgroup changes are not bridged.
- No AMBE transcoding. A transmission that is not AMBE+2 will not become one.
- DMR embedded LC and the voice LC header are generated for the configured source and talkgroup. Talker alias is not sent.
- The DMR sync pattern is the mobile-station pattern used by YSF2DMR. MMDVMHost normally rewrites sync for the repeater; a peer that transmits the network frame unchanged may need the base-station pattern instead.
- Callsign lookup is case-insensitive and uses the first ID in the file. It is not reloaded until the process restarts.
- The bridge does not register the reflector or exchange routing. Both ends must be configured by hand.
- IPv4 and IPv6 addresses work for the peer, but the link is still a single static target.

The AMBE repack, Golay, FICH, and DMR LC code is the GPL v2 implementation from MMDVM and YSF2DMR (Jonathan Naylor G4KLX, Andy Uribe CA6JAU, and the other copyright holders named in those files).
