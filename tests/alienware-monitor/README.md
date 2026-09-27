# Alienware monitor regression tests

Run `python tests/alienware-monitor/run.py` from an OpenRGB checkout, with a C++17 `g++` or `clang++` on PATH (or set `CXX`). The runner compiles the actual protocol, profiles and controller with link-time HID stubs. It **cannot open USB devices**. It also compiles the detector and RGB wrapper against actual OpenRGB headers with `-fsyntax-only`.

The checked-in `fixtures.json` contains 44 distinct complete HID reports (Report ID zero plus 192 captured USB bytes), and 101 captured challenge/response pairs for AW3423DWF and AW2724DM. Color fixtures cover historical DWF/DM and AW3426DW AWCC/SignalRGB captures. Each record retains its capture name, SHA-256 and frame number. Both challenge parity branches are covered. These are protocol regression fixtures; USB success does not establish that every captured historical color was visibly applied.

Additional tests cover profile uniqueness, two/four-zone masks, exclusion of non-RGB/unknown PIDs, checksum and 65-byte Microchip/legacy framing, zero Report ID, encoded key bounds, short/error reads, partial writes, failed probes, bounded key fallback, fresh authentication before each color, cooldown after errors, key state isolation across devices, and preservation of the existing AW3225QF initialization sequence.

Sources and boundaries:

- Base: OpenRGB GitLab `cd44e41d462629410a806119694a773e88449ce1` (2026-09-18).
- Classic transport: SignalRGB `Alienware_Monitor_Gen1_Controller.js`, VID 0424 and PID 274A/274B/274C. This supplies legacy 92/48 packets, without a DDC checksum.
- Microchip: existing OpenRGB AW3225QF implementation plus SignalRGB Gen2. Existing AW3225QF initialization is preserved; newly added Microchip profiles use the Gen2 F4 query. F4 is a query, not evidence of a special software-mode switch.
- Profiles, RGB zones and authentication flags: installed AWCC `FxDisplayCommon.dll` 6.14.24.0, `HIDDeviceFactory`, `AnimationBase.GetLightZoneBitmap` and per-model `FxMetaData`.
- Realtek: current AWCC's E1 challenge/response and C6 DDC framing, supported by local historical/current captures. All five encoded OEM key candidates are retained. AW2724DM tries its captured key first; others use AWCC's first key first. Handshake plus `51 82 01 C8 74` probe reproduces AWCC's transport test; a successful HID write alone is not independent cryptographic or visible-light confirmation. Selection is cached per controller and invalidated on I/O failure.
- Only AW3426DW has been visibly checked with SignalRGB during this session. The OpenRGB implementation has not been run against hardware. New profiles remain candidates pending device-owner validation.
- No full OpenRGB GUI build or cross-platform runtime test is claimed by this test runner.

`generate_profiles.py MATRIX.json` optionally regenerates the profile/detector tables from the separately supplied evidence matrix. `extract_fixtures.py ARCHIVE_DIRECTORY` optionally regenerates fixtures from the original local analyses and captures; it is not needed for normal offline tests.

The AW3426DW default interval is now100ms as a conservative response to a later user report of lag. The earlier20ms setting and successful USB acknowledgements are historical evidence only. The new100ms interval has not yet been optically validated, and OpenRGB has not been run on the monitor.
# Scheduling and live SignalRGB parity

The worker regression also verifies that rendering 10,000 replacement frames
does not enqueue 10,000 USB writes. Dirty zones rotate fairly, equal colors
share a mask, repeated colors are skipped for two seconds, and shutdown wakes
the worker before closing the HID handle. These checks use link-time HID stubs.

To additionally compare against an installed set of the three SignalRGB plugins:

```powershell
python tests/alienware-monitor/run.py C:\path\to\SignalRGB\Plugins
```

The optional comparison runs the actual JavaScript packet builders in a Node VM
with no hardware API. It compares all 23 VID/PID profiles, auth flags, intervals,
zone masks, 2,496 color reports and 320 responses across all five OEM keys against
the compiled C++ implementation. This checks protocol equivalence, not physical
validation of monitors that are not present. AW3225QF retains the upstream native
initialization sequence; the common color packet parity is tested separately.
