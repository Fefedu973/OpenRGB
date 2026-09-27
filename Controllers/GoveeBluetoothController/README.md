# Native Govee Bluetooth controller for Windows

This controller provides **one RGB zone** for the explicit `h6008-realtime-v1`
and `h6159-classic-v1` profiles. It uses C++/WinRT directly from OpenRGB. No Python
process, loopback bridge, cloud service or LAN color fallback is involved.

The implementation is based on the independently established packet/state
behavior in the companion below. Native tests on 27 September 2026 validated
initial connection, RGB acquisition and restoration on three H6008 bulbs and
one H6159 strip. An H6008 reconnection defect was then isolated and corrected:
the device can retain its authenticated session after Windows GATT objects
close. Repeating E701 in that state is ignored, whereas an identity query with
the previous session credential succeeds. The native transport now verifies
that credential before resuming. Three consecutive connections on each of the
three bulbs passed, including all 27 state queries. A further three cycles
destroyed/recreated the complete transport on one bulb and passed all nine
queries, exercising the same-process handoff needed by rescan. Evidence and remaining
scope limits are documented in [the reconnection probe](tests/RECONNECT-PROBE.md).

## Build and offline validation

MSVC x64, C++17 and Windows SDK 10.0.19041 or newer are required. The transport
uses the SDK's C++/WinRT headers. `windowsapp.lib` and `bcrypt.lib` are linked by
MSVC `#pragma comment(lib, ...)`; no OpenRGB.pro modification is necessary.
The `_Windows` filenames follow OpenRGB's existing platform source selection.
Other platforms do not register a detector for this transport.

From PowerShell:

```powershell
& .\Controllers\GoveeBluetoothController\tests\run-msvc-tests.cmd
& .\Controllers\GoveeBluetoothController\tests\compile-msvc.cmd
```

The first builds/runs only fake-radio protocol/session tests and a Windows CNG
crypto test. The second compiles the native source against OpenRGB headers with
`/c`; it never starts OpenRGB or opens a Bluetooth device. Output goes to
`%TEMP%\OpenRGB-Govee-BLE-tests`, or `GOVEE_BLE_TEST_OUTDIR` if explicitly set.
Tests use `.cc` so OpenRGB's recursive `Controllers/*.cpp` glob does not add a
test `main()` to the application.

Offline coverage: exact 20-byte/XOR packets, strict configuration fields,
brightness scaling, static RGB/white restoration, orphan-mode recovery and
failed recovery readback, reconnects, repeated-color suppression, external OFF,
H6159 owned blackouts and ordered resume, original raw brightness and secondary
RGB flag restoration, unsupported modes, AES128 known answer with independently
checked RC4 tail, and 256 synthetic encrypted packet round trips. Fourteen additional
authentication tests cover retained-session identity, fresh authentication after
a simulated reset, lost replies, per-device isolation, wrong-identity refusal,
cache expiry, bounded eviction and complete transport recreation.
Native hardware
observations apply to the tested units; neither these tests nor nominal
intervals are optical frame-rate measurements.

## Configure explicit devices

The normal OpenRGB settings JSON accepts a locally scoped section:

```json
{
  "GoveeBluetooth": {
    "config_file": "C:/private/govee-ble-devices.json"
  }
}
```

The detector registers this section with `RegisterSettingsSchemaLocalOnly`.
It loads at most 16 entries from that separate private JSON file. Start from
`config.example.json`, replace the synthetic addresses, then enable only the
devices to control. A rescan/restart is needed after changing this file.

- `profile`: exactly `h6008-realtime-v1` or `h6159-classic-v1`.
- `address`: the configured BLE MAC, not the Wi-Fi MAC. The old companion's
  `ble_address` spelling is also accepted.
- `name`: optional friendly label, at most 80 bytes, no line breaks.
- `enabled`: optional boolean, default `true`. Examples are explicitly disabled.
- H6008 additionally requires `wifi_mac` for the authenticated AA14 identity
  check, and `key_file`, an absolute file path containing the private 16-byte
  communication key as 32 hexadecimal digits (surrounding whitespace allowed).
  A shared top-level `key_file` in the private JSON is also accepted.
- `power_on_acquire`: optional boolean, default **`true` for H6008** and
  **`false` for H6159**. Its first acquisition may turn an initially OFF device
  ON; mode/color are preloaded first. An initial black frame defers that ON
  until effective RGB becomes nonzero. Set it to `false` to preserve initial
  OFF. Later manual OFF is respected, including after a connection retry.

The key is never part of an OpenRGB setting, SDK controller descriptor, source
file, example or log. Private config and key paths must be absolute; files are
bounded to 64 KiB and 256 bytes respectively. Invalid entries are skipped with
an entry-number diagnostic that does not echo their contents. Configuration is
manual: there is no scan that adds unrelated nearby lights.

Remove the same devices from OpenRGB's `GoveeDevices` LAN list before enabling
their Bluetooth profiles. Stop other BLE owners (the old companion, SignalRGB
BLE client and the phone app) for native testing/use. A named Windows mutex
prevents a second instance of **this OpenRGB controller** from owning the same
BLE address; other applications do not participate in that mutex.

## Session behavior

Each configured device has one MTA worker thread. `DeviceUpdateLEDs` replaces
one pending frame under a mutex and returns without doing Bluetooth I/O. There
is no growing frame queue. H6008 has a 50 ms minimum spacing, H6159 100 ms;
GATT latency adds to those intervals, so these are ceilings of 20/10 updates
per second, not promised optical rates. Identical frames are suppressed.

Connections begin on the first submitted frame. Every reconnect fetches both
services and characteristics with `BluetoothCacheMode::Uncached` and verifies
the required notification/write properties. H6159 FW 1.07.02 is a measured
exception: its characteristic may advertise only WriteWithoutResponse/read,
while its successful command/status path uses ATT WriteWithResponse. The
H6159 profile accepts either writable property and still sends WriteWithResponse;
this exception is not applied to other profiles or non-writable characteristics.

A client-owned `GattSession` is retained with `MaintainConnection=true` until
disconnect. After discovery, Active status is awaited with a bounded deadline;
disconnect first attempts CCCD None (at most 250 ms of polling), revokes its
notification callback, and permits up to 100 ms of interruptible settling
before closing service objects. It then releases maintenance and closes the
session. The settling interval follows Bleak's documented WinRT cleanup
workaround. Both waits obey shutdown/restoration cancellation; synchronous
Windows `Close` itself has no caller-enforceable time limit. This follows the
WinRT lifetime pattern used by Bleak. Microsoft also documents that uncached
discovery itself can initiate a connection, so absence of a retained session
was a lifecycle difference, not proof of the observed radio failures.
Notifications use a bounded inbox
whose lifetime is independent of the controller. Invalid checksums, lengths
and unrelated responses cannot satisfy a query. All asynchronous calls and
response waits are bounded and observe shutdown cancellation.

H6159 discovery enumerates the configured device's complete uncached service
table and requires exactly one matching Govee UUID. This addresses the observed
filtered query returning Success with zero entries; the complete-table path
subsequently passed native command/readback/restoration tests. A transport
already connected/authenticated by a caller is
reused for the initial snapshot and acquisition rather than disconnected and
authenticated a second time.

H6008 initially authenticates with E701/E702 (one retry for the first E701 reply),
then checks the exact AA14 Wi-Fi identity before any mode/color write. On later
GATT connections, a fresh AA14 first tests its previous session credential.
Only an exact identity match permits reuse. A reply timeout falls back to the
bounded fresh E701/E702 exchange. A missing E702 or AA14 reply retains the new
key as an unverified candidate, so the next attempt can validate it without
blindly restarting an already completed handshake. Lost replies do not erase
the last verified credential; a different identity clears candidates and aborts.
Credentials remain only in native memory and are never written to logs or
configuration. For rescan, an authenticated transport hands its candidates to
a process-memory cache scoped by exact BLE address, profile, AA14 identity and
root authentication credential. It contains at most 16 entries, with a five-
minute monotonic TTL checked on every access. A new transport consumes its
entry before validation; failed verification cannot leave that unchecked entry
available to another controller. Entry expiry/eviction/destruction wipes key
bytes, and a shared lifetime prevents static shutdown from invalidating a
worker's cache. Local candidates are wiped when their transport is destroyed.
No external process or bridge performs recovery. It sends the
established mode05 realtime packets. The first restorable mode0D response is
preserved exactly, including white-temperature fields. If the device was left
in mode05 by a crashed/rebooted owner, the explicitly submitted RGB establishes
a **new verified static baseline**. It is not the unknown color from before
that reboot. Unknown scenes fail closed. By default, first acquisition of an
OFF bulb prepares mode05 and the current scaled RGB before sending Power ON.
Initial black or zero brightness defers that ON. It is not repeated on every
frame/reconnect: an externally switched OFF bulb subsequently pauses until
switched ON externally. If this acquisition owned the initial OFF-to-ON
transition, release restores OFF before the original mode. A bulb already ON
is not power-owned by OpenRGB. Hardware brightness is never changed; OpenRGB
brightness scales RGB. This acquisition policy has fake-radio regression
coverage and passed the native initial-acquisition tests on the three bulbs.

H6159 uses plain mode02 RGB packets and WriteWithResponse, preserving the full
supported mode payload, alternate RGB flag, raw brightness and initial power.
Every color uses ATT WriteWithResponse. Application-level AA05 readback checks
the first RGB of every connection, every forced preload before ON, and the
last sent RGB every 1.5 seconds (including when the color is static). Intermediate
animation frames do not each add an AA05 request. The periodic check occurs
before the next color write so an unexpected mode is not hidden by that write.
Black RGB or brightness0 switches the strip OFF only after confirming it was
ON. Only an OFF owned by this blackout permits automatic resume; its newest
RGB/brightness is written and checked before switching ON. An independently
switched-off strip is left OFF. This is one RGB color for the entire strip,
not addressable LEDs and not an anti-fade protocol.

The H6159 native sequence submitted 63 frames including a 60-color animation
segment in approximately 12.154 s (about 4.94 animation updates/s), compared
with about 3.65/s when every color queried AA05. Restoration of power, raw
brightness and the complete supported mode payload was verified. A separate
H6008 sequence completed 60 animation updates in 4.969 s (about 12.1/s) and
verified those three restoration fields. These are transport/test timings,
not measurements of LED refresh or guarantees for another Windows adapter.

Recoverable failures reconnect after 1 second; after three consecutive failed
steps the backoff is 30 seconds. A successful step clears that failure count.
An H6008 AA01 power-state read with a missing reply is retried once on the same
authenticated connection before that reconnect path. Other H6008 reads remain
single-attempt, and H6159 retains its existing one-retry read policy. Each reply
wait is bounded to three seconds; the retry adds at most one such wait and one
bounded GATT write. Cancellation and the shared shutdown deadline still abort
without an additional attempt. After authentication, a closed GATT session also
aborts the receive loop before its next 25ms wait, as a connection error rather
than a reply timeout. A closed link is therefore not retried by this policy;
initial E7/AA14 authentication is exempt from this session-closure check. This
shortens recovery after a confirmed closure; it does not establish or fix the
cause of the periodic disconnects observed during the full-device live run.
No mode, color, power command or authentication
handshake is replayed by the query retry. A recovered read is logged explicitly;
two missing replies are reported with the attempt count and numeric connection/
session state before normal reconnection. Per-connection counters distinguish
missing replies, retries and successful recoveries. Offline tests cover recovery,
exhaustion/reconnection, no extra color writes and cancellation. This change
addresses tolerance of lost status replies; its effect on the observed periodic
AA01 loss has not yet been verified on hardware.
The original snapshot persists across reconnects. Logs report meaningful state
changes and bounded errors, without dumping authentication packets.
Timeout diagnostics identify only the expected command/prefix and counts of
received, invalid-checksum or unrelated notifications. GATT failures report
their numeric communication status. No key, nonce, identity payload or address
is included in these diagnostic details.

On normal controller destruction the render thread shuts down, the worker
stops accepting frames and attempts restoration on its existing connection
within a shared five-second budget. It does **not** reconnect or power on a
device merely to restore it. If the radio is unavailable, Windows terminates
the process, or OpenRGB crashes, restoration is not guaranteed; this limitation
is reported rather than represented as a successful reset.

## Protocol provenance and primary API references

- Protocol and recovery behavior: [SignalRGB Govee companion](https://github.com/Fefedu973/signalrgb-govee-direct-connect/tree/main/ble-companion),
  especially `protocol.py`, `transport.py`, `profiles.py`, `bridge.py` and
  `classic_session.py`. The upstream MIT notice is retained in
  `THIRD_PARTY_NOTICES.md`; the new OpenRGB integration is GPL-2.0-or-later.
- [Microsoft GATT client example](https://github.com/microsoft/Windows-universal-samples/blob/main/Samples/BluetoothLE/cppwinrt/Scenario2_Client.cpp)
  demonstrates uncached service/characteristic discovery and notifications.
- [Microsoft GattDeviceService API](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.genericattributeprofile.gattdeviceservice)
  documents UUID and cache-mode discovery.
- [Microsoft C++/WinRT asynchronous operations](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/concurrency-2)
  explains operation status, cancellation and bounded waits. This worker polls
  status before `GetResults`, never calls an unbounded `.get()`.
- [Bleak's WinRT client](https://github.com/hbldh/bleak/blob/develop/bleak/backends/winrt/client.py)
  retains `GattSession`, requests `maintain_connection` and waits for its Active
  state after service discovery. The locally installed client was also compared.
- [Microsoft GATT connection behavior](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/gatt-client)
  distinguishes maintaining a session from connection attempts triggered by
  uncached discovery or attribute I/O.

No Elgato/NVIDIA binaries, private Govee keys, device addresses or captures are
part of this controller.
