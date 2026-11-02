# DVReflectors
This repo contains the reflectors originally from G4KLX's NXDNClients, P25Clients, and YSFClients repositories.

The NXDN and P25 reflectors are used as a single talkgroup. The NXDN reflector also includes the option to link it to NXCore to allow for interchange of audio between the two. At the NXCore end, it should be set up to receive the traffic from only one talk group.

They build on 32-bit and 64-bit Linux as well as on Windows using Visual Studio 2017 on x86 and x64.

YSFReflector can optionally link to one HBlink3/FreeDMR OpenBridge peer and bridge a single DMR talkgroup without a separate YSF2DMR process. The link is off unless `[OpenBridge] Enable=1`. See [YSFReflector/OPENBRIDGE.md](YSFReflector/OPENBRIDGE.md) for configuration, an HBlink example, and what still needs on-air testing. `make test` in `YSFReflector/` runs the loopback harness.

This software is licenced under the GPL v2 and is primarily intended for amateur and educational use.
