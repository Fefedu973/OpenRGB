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
clock and radio boundary, plus the production GoveeDirectControl policy. Twenty-one
assertions pass: initial 42-segment frame/brightness before ON; up to three ON
attempts at 0/1/2 seconds (never after the three-second acquisition window);
120-second streams at both 50 FPS and the 1 FPS keepalive cadence without
B1 or unchanged brightness after acquisition; immediate changed brightness;
one B1/brightness refresh after an actual gap of at least thirty seconds, with
no repeated ON and no further reactivation during resumed output; explicit mode selection
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
makes shutdown interruptible. Normal B0 frames, including the one-second idle keepalive, maintain output
without periodically re-entering the mode. B1 and brightness are resent only
after an actual thirty-second gap between output frames (for example suspend),
never every ten seconds during a healthy stream. Brightness also updates when
its requested value changes. There are no further explicit
ON commands after acquisition unless Direct is selected again. This preserves
the command-level distinction between initial acquisition and later manual OFF;
there is no continuous device-status acknowledgement or physical OFF guarantee
in this UDP-only driver. Color packet encoding and model segment counts are
unchanged; no BLE path or runtime settings are touched by these tests.

A prior revision unconditionally resent B1 and brightness every ten seconds.
That could interrupt the device rendering even while normal B0 frames continued;
the timer has been removed. These offline tests verify command sequencing, not
physical confirmation of the reported intermittent black flashes.
