# Separate local candidate

Build/package the three source trees first with `tools/room-build/Build-Room.ps1`.
The portable result is `dist-room`. It is not installed or enabled at login.

Use a new private configuration directory, never the normal OpenRGB directory.
For synthetic image work, copy the `VirtualScreens` example from
`Controllers/VirtualScreenController/config.example.json` into its `OpenRGB.json`.
Enable the example outputs, then run:

```powershell
.\tools\room-setup\Start-Candidate.ps1 -ConfigDirectory C:\private\room-test -VirtualOnly
```

This mode registers only explicit virtual screens, skips saved network clients
and refuses subsequent physical rescans. It can run alongside SignalRGB.

For a hardware candidate, close SignalRGB and other RGB owners and suspend the
old bridge supervisor first. The launcher refuses known conflicting processes;
this is a convenience check, not a universal inventory of hardware owners.
Keep Elgato and its background compositor running when testing the native
Stream Deck adapter. Omit `-VirtualOnly` only for the planned hardware test.

Copy private Govee companion settings with `import-govee.py` into a new private
directory, then reference its generated config using the `GoveeBluetooth`
section documented in the driver README. The importer never changes the old
bridge. Configure other devices and strip lengths only after enumeration.

The SDK integration test creates its own temporary settings and never uses this
hardware candidate. Existing SignalRGB layouts and registry are untouched.
