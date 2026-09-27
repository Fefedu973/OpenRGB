# Brief black flashes on Govee LAN strips

The user reported intermittent, fraction-of-a-second black flashes while the
Wi-Fi strips otherwise followed the effect. The preceding recovery change sent
the B1 Direct-mode activation command and unchanged brightness every ten seconds,
including during uninterrupted B0 color output. Re-entering the mode is a plausible
render reset; a local packet capture alone cannot establish the firmware's optical
response or exclude a wireless delivery failure.

A 24-second capture of outgoing UDP port 4003 covered all nine configured LAN
devices: 1,706 decoded B0 frames, no entirely black frame, no invalid length or XOR
checksum, 27 B1 enable commands and 27 brightness commands at 100 percent. Each
strip received three activations spaced 10.001–10.005 seconds apart. No OFF command
was present. Capture metadata reported a flushed packet per interface; these counts
describe captured packets, not a lossless radio trace. Captures contain private
network identifiers and remain outside version control.

The correction retains the three bounded acquisition attempts. During continuous
output, it sends color frames and only changed brightness; it does not periodically
re-enter Direct mode. A real output gap of at least thirty seconds permits one
mode/brightness reacquisition without forcing power ON. The one-second worker
keepalive resends the current color frame, including for a static Direct color.
This does not add a device-status acknowledgement or claim to repair every possible
firmware/network failure.

All 21 production-method assertions passed, covering prolonged continuous output,
one-frame-per-second maintenance, brightness changes, startup retries, and
reacquisition after an actual output gap. The same test fails against the previous
policy at the continuous-output/no-reactivation assertion. The complete Windows
core was rebuilt after regenerating its header dependencies, packaged and started
through the existing Windows task. See
[LAN test instructions](../../tests/room-govee-lan/README.md).

The post-installation 24-second capture contains 1,681 valid B0 color frames across
all nine LAN devices, with **zero B1 activations, brightness commands, ON/OFF
commands, black frames or malformed frames**. The capture reported no dropped
packets. This verifies the running binary's outgoing traffic; it does not prove
delivery at the strips. The user subsequently watched the installation for about
thirty seconds and confirmed that the black flashes had disappeared. Longer-term
radio reliability was not measured by this short observation.

Runtime SDK validation also found all 30 controllers, both H61A2 strips, 28 changing
color buffers and both native image outputs. The original SignalRGB layout,
OpenRGB Full Scale placements and Bluetooth transport are unchanged.
