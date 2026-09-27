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

Production-method tests cover prolonged continuous output, one-frame-per-second
maintenance, brightness changes, startup retries, and reacquisition after an actual
output gap. See [LAN test instructions](../../tests/room-govee-lan/README.md).

Post-installation packet verification and the user's visual result are recorded
when available. The original SignalRGB layout and Bluetooth transport are unchanged.
