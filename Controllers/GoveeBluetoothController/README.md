# Native Govee Bluetooth controller for Windows

This controller provides **one RGB zone** for the explicit `h6008-realtime-v1`
and `h6159-classic-v1` profiles. It uses C++/WinRT directly from OpenRGB. No Python
process, loopback bridge, cloud service or LAN color fallback is involved.

The implementation is based on the independently established packet/state
behavior in the companion below. **This C++ port has offline tests; it has not
yet been validated against hardware.** Existing Python/SignalRGB hardware
validation does not establish that this new transport works on a given PC.

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
checked RC4 tail, and 256 synthetic encrypted packet round trips. GATT behavior
and timing still require a controlled native hardware test.

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
- H6159 accepts `power_on_acquire` (default `false`). If enabled, only its first
  acquisition may turn an initially OFF strip ON; color is preloaded first.
  An initial black frame defers that explicitly opted-in ON until nonzero RGB.
  H6008 rejects this option because its established profile does not own power.

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
the required notification/write properties. Notifications use a bounded inbox
whose lifetime is independent of the controller. Invalid checksums, lengths
and unrelated responses cannot satisfy a query. All asynchronous calls and
response waits are bounded and observe shutdown cancellation.

H6008 authenticates with E701/E702 (one retry for the first E701 reply), then
checks the exact AA14 Wi-Fi identity before any mode/color write. It sends the
established mode05 realtime packets. The first restorable mode0D response is
preserved exactly, including white-temperature fields. If the device was left
in mode05 by a crashed/rebooted owner, the explicitly submitted RGB establishes
a **new verified static baseline**. It is not the unknown color from before
that reboot. Unknown scenes fail closed. This profile never changes power or
hardware brightness; OpenRGB brightness scales RGB, and externally OFF bulbs
pause until they are switched ON externally.

H6159 uses plain mode02 RGB packets and WriteWithResponse, preserving the full
supported mode payload, alternate RGB flag, raw brightness and initial power.
Black RGB or brightness0 switches the strip OFF only after confirming it was
ON. Only an OFF owned by this blackout permits automatic resume; its newest
RGB/brightness is written and checked before switching ON. An independently
switched-off strip is left OFF. This is one RGB color for the entire strip,
not addressable LEDs and not an anti-fade protocol.

Recoverable failures reconnect after 1 second; after three consecutive failed
steps the backoff is 30 seconds. A successful step clears that failure count.
The original snapshot persists across reconnects. Logs report meaningful state
changes and bounded errors, without dumping authentication packets.

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

No Elgato/NVIDIA binaries, private Govee keys, device addresses or captures are
part of this controller.
