# Logitech receiver regression tests

Run `Build-Tests.cmd` from a Visual Studio Build Tools installation. `VCVARS`
can select another MSVC environment. The executable is written to ignored `.build`.
No HID library is linked and no device is opened.

The test executes the same `LogitechReceiverIdentity.h` implementation used by
the detector, with two separate synthetic C547 receivers:

- G915 TKL and G502 X PLUS, enumerated in both orders, with different discovered
  feature indexes and separate Windows short/long collections;
- case-insensitive Windows collection grouping without merging physical
  instances, and no reinterpretation of Linux/macOS paths;
- correct interface, vendor usage page and usage filtering (including rejection
  of LampArray and a usage whose truncation would otherwise alias usage 2);
- only 20-byte long reports accepted by the simulated usage-2 transport;
- full paginated names, unrelated slot/app responses and connection notifications;
- failed writes, timeout, missing feature, malformed/truncated/nonprintable names,
  correlated errors and a bounded stream of unrelated responses.

It also exercises `LogitechReceiverProtocol.h`: a receiver ACK followed by a
timeout cannot invent slot 0; connection notifications must identify a real
slot/WPID; missing notifications fall back to correlated pairing-register
reads. Simulated Windows long replies exist only on usage 2, including pairing
information, name and serial. Two receivers remain isolated in both orders,
and notification flags unrelated to wireless connection reporting are preserved.

MSVC x64 `/std:c++17 /W4`: passed on 2026-09-27. The real
`LogitechControllerDetect.cpp` and `LogitechProtocolCommon.cpp` also passed MSVC `/Zs` using the current core Qt
6.8.3 build flags. This is source/protocol validation, not a hardware coexistence
test. See [the audit](../../docs/room/logitech-coexistence.md).

## Opt-in hardware identity probe

`Build-Probe.cmd` compiles `receiver-probe.exe` against the repository's hidapi
dependency but does **not** run it. Unlike the mock tests, this tool opens real
hardware only when explicitly invoked with `--probe-read-only`.

Before that invocation, stop the existing RGB owner, including any OpenRGB
Windows service. The tool refuses while known SignalRGB/OpenRGB/G HUB processes
are listed. It limits access to `046D:C547`, interface 2, page `0xFF00`, usage 2.
Only read queries are emitted: IRoot feature discovery, device name and the
three `0x8081 GetInfo` bitmap pages. It does not enable wireless notifications,
request a reconnect, claim software control, set lighting or modify profiles.
Output contains capability data without USB paths or serial numbers.

The opt-in probe ran on 2026-09-27 after the existing OpenRGB Windows service
was stopped. After an initially unanswered attempt, bounded IRoot queries
(`--root-slot-scan`) confirmed slot 1. The next full read succeeded: G502 X PLUS,
8071 index 9 v2, 8081 index 10 v2, zones 1..8. No state-setting command was
used. The initial silence has no proven cause (sleep is possible). Sanitized
results are in validation.json. This confirms identity/capabilities, not
rendered lighting or physical G915 coexistence.
