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
