# Govee LAN discovery regression test

Run `Build-Tests.cmd` with MSVC x64 installed. This builds the real
`GoveeController.cpp` and `net_port.cpp`; it does not launch OpenRGB.

All transmitted packets go to 127.0.0.1/2/3. The multicast destination is redirected
to 127.0.0.2 only in the fixture; production also sends its unchanged multicast
scan and a targeted unicast scan to each configured IPv4 on 4001, from the shared
socket bound to 4002. The fixture needs ports 4001/4002/4003 free on those addresses
and fails without stopping any existing process if they are occupied.

Checks cover both scan destinations, their identical JSON and source port,
rejection of an incorrect MAC reusing the configured IP, malformed SKU rejection,
discovery by stable MAC at a changed address, metadata, subsequent control routed
to that discovered loopback address, immediate discovery bind failure, callback
removal and closure of the controller's actual send socket. It sends no retry and
does not assert that multicast works on a physical LAN.

Validation on Windows/MSVC x64: 21 assertions passed on 27 September 2026.

## Direct-mode acquisition and maintenance (no sockets)

From an MSVC x64 developer prompt:

```text
python tests/room-govee-lan/test_direct_control.py --out tests/room-govee-lan/.build
```

This compiles the actual RGBController_Govee update methods with only a fake
clock and radio boundary, plus the production GoveeDirectControl policy. Fifteen
assertions pass: initial 42-segment frame/brightness before ON; up to three ON
attempts at 0/1/2 seconds (never after the three-second acquisition window);
independent B1/brightness refresh despite a continuous 50 FPS effect; immediate
changed brightness; no repeated ON after an idle gap; explicit mode selection
starts a fresh acquisition; empty frames cause no activation; and static-mode
traffic retains its thirty-second maintenance interval.

The old Direct path never sent ON, enabled B1 only at mode entry, and refreshed
it only after thirty seconds without RGB updates. A lost activation packet could
therefore persist throughout an effect. It also sent unchanged brightness on
every RGB frame. These are code-level findings, not proof of the cause of any
particular disconnected or unconfigured strip.

The keepalive thread now only queues a core LED update once per second after the
first update; it never reads color/mode state or transmits UDP itself. A send
mutex serializes the derived controller's update paths, and a condition variable
makes shutdown interruptible. B1 and unchanged brightness are refreshed every
ten seconds independently of incoming RGB frames. There are no further explicit
ON commands after acquisition unless Direct is selected again. This preserves
the command-level distinction between initial acquisition and later manual OFF;
there is no continuous device-status acknowledgement or physical OFF guarantee
in this UDP-only driver. Color packet encoding and model segment counts are
unchanged; no BLE path or runtime settings are touched by these tests.
