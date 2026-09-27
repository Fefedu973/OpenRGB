# H6008 same-process reconnection probe

Build only with `compile-reconnect-probe.cmd`. It writes `reconnect_probe.exe` to
`%TEMP%\OpenRGB-Govee-BLE-tests` (or `GOVEE_BLE_TEST_OUTDIR`). It does not run it.

Stop OpenRGB, SignalRGB, the old BLE companion and other BLE owners before
explicitly running:

```powershell
& "$env:TEMP\OpenRGB-Govee-BLE-tests\reconnect_probe.exe" --read-only C:\private\govee-ble-devices.json 0 2 0
```

Arguments after the private configuration are its device index, cycles (1–3),
an optional gap in milliseconds (0–10000), and an optional comparison variant.
The default two cycles have no
extra gap. This gap is a diagnostic comparison parameter, not a production
reconnection policy. The probe only opens the explicitly configured H6008.

Variants: `current` reuses one transport; `full-services` enumerates all services
instead of a UUID-filtered query; `auth-write-response` changes only the E7
authentication writes to ATT WriteWithResponse; `new-object` reconstructs the
transport every cycle; `new-mta` also creates and joins a fresh MTA thread with
balanced apartment initialization/uninitialization every cycle. Finally,
`clear-factory-cache` also clears the C++/WinRT activation factories after all
transport objects are destroyed and before apartment uninitialization. None changes
production configuration or the default native behavior.

The exact production `WindowsTransport` is used. Each cycle authenticates,
subscribes, reads the CCCD and queries power/brightness/mode, then disconnects.
There are **no `0x33` lighting-state writes** and no RGB/mode/power change.
Authentication and CCCD writes are necessary to perform these reads. A native
ownership mutex and a process check prevent known OpenRGB/SignalRGB conflicts;
other BLE clients do not participate in that mutex and must be stopped manually.

JSON contains timing, numeric Windows states, CCCD result and notification
counts only. No addresses, key material, authentication/state payloads or private
paths are printed. `notificationEvents` counts all callbacks, including non-20-
byte notifications; the production reply parser counts only valid-length packets.

Status conventions: `connectionStatus` is the Windows BluetoothConnectionStatus
(0 disconnected, 1 connected); `sessionStatus` is GattSessionStatus (0 closed,
1 active). `-1` means the local object is absent, `-2` a property/result failure.
CCCD statuses use GattCommunicationStatus (0 success); unsubscribe `-3` means the
short wait was cancelled/timed out. CCCD value 1 is Notify and 2 is Indicate.
Objects becoming absent **does not prove a radio disconnect**. Microsoft notes
that disposing the last reference triggers disconnection only after a short
system-managed timeout. This probe does not force-disconnect the radio.

Primary comparisons:

- [Microsoft GATT client lifecycle](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/gatt-client)
- [Bleak WinRT implementation](https://github.com/hbldh/bleak/blob/develop/bleak/backends/winrt/client.py)

The production transport performs the existing short CCCD None/settling cleanup.
This probe introduces no further production retry, cache or sleep changes.

## Native observations, 27 September 2026

One configured H6008 was probed with two cycles per comparison, without any
lighting-state writes. The current transport passed its first connection,
identity check and three state queries, then failed its second E701 with zero
notification callbacks. This repeated both with no added gap and with a 2 s
gap. CCCD subscription and unsubscription reported Success; a first-cycle CCCD
read confirmed Notify. The absence of all callbacks rules out only reply
parsing/checksum rejection as the immediate reason in these attempts.

Full-service enumeration, a new transport object and a new MTA thread each
gave the same first-cycle success/second-cycle failure. ATT WriteWithResponse
for E7 was rejected with `0x80650003` (write not permitted) even on the first
cycle and is not suitable for this H6008 path. No comparison is promoted as a
reconnection fix. The exact cause of the missing second-session notifications
remains unresolved; Windows radio connection lifetime and firmware session
state are hypotheses, not established conclusions. A fresh process continued
to succeed on its initial connection between these comparisons.

The final isolated `clear-factory-cache` comparison completed in 7.744 s under
a 30 s process deadline. Cycle 1 passed with six notifications; cycle 2 again
timed out at E701 with zero callbacks despite successful CCCD subscription.
Clearing activation factories is therefore not a demonstrated fix either.
Hardware investigation stopped after this bounded comparison. No process
bridge, adapter reset or production cache-clearing policy was introduced.
