# Explicit native hardware diagnostics

`build-native.cmd` builds `native_devices.exe` from the same production
C++/WinRT Govee transport, protocol/session, KBHE HID and Alienware HID sources
as OpenRGB. Only the logging boundary is substituted. There is no simulated
radio or USB device, Python bridge, OpenRGB detection registry or SMBus scan.
Without `--exercise` it prints usage and does not access hardware.

Stop SignalRGB, OpenRGB **including its Windows service**, and the previous
bridge supervisor before exercising devices. The Govee named mutex also
prevents two native owners of the same BLE address. Never run these tests
concurrently with the normal drivers.

```powershell
& .\tools\room-diagnostics\build-native.cmd
& .\tools\room-diagnostics\.build\native_devices.exe --exercise govee C:\private\devices.json 0
& .\tools\room-diagnostics\.build\native_devices.exe --exercise kbhe
& .\tools\room-diagnostics\.build\native_devices.exe --exercise alienware
```

The device index refers to the explicitly supplied private BLE allowlist.
The diagnostic may turn on an initially off light for its bounded test, then
verifies its original power and brightness on the authenticated connection just
before it closes. Immediate re-authentication after close is a separate
reconnect check, not a prerequisite to observing restoration. It exercises
red/green/blue and a short rainbow; H6159 also tests the zero-brightness blackout
and resume. H6008 already left in realtime mode by another process has no
readable prior RGB; the production session establishes a first-frame baseline
and reports that limitation instead of claiming to restore an unknown color.

The USB targets are deliberately narrow: KBHE 9172:0002 RAW HID interface 1,
FF00/1; AW3426DW 187C:101D FFDA/DA. Multiple matches are rejected. KBHE sends
90 full frames and attempts restoration. AW3426DW submits a short two-zone
rainbow through the latest-frame worker at the model's 100 ms transfer interval.
Alienware does not expose its original color through this protocol; restart the
previous RGB owner after the test to restore its effect.

Transfer success is not an optical verdict. Keep the JSON result and ask an
observer to confirm the lighting. Private addresses and keys are not printed.
