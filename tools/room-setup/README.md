# Native room setup and isolated testing

The daily setup uses `Install-NativeStartup.ps1` (PowerShell 7, administrator).
It validates the prepared Full Scale map and optional executable SHA, saves old
startup definitions, installs a direct `OpenRGB.exe` task and retires conflicting
SignalRGB/OpenRGB startup entries. No Python supervisor is installed. The existing
Wallpaper Engine bridge is deliberately preserved. `Demarrer-OpenRGB.cmd` starts
that task on demand; an already-running instance is not duplicated.

The machine-local configuration is `private/native-configured`; it is excluded
from Git because it contains device identities and links to private BLE keys.
The source SignalRGB Full Scale and registry remain unchanged. The installer
requires `migration-ready.json` with `complete:true` and the validated
`full_scale_sha256`, and records success/failure in `startup-install-result.json`.
`Collect-RamInventory.ps1` provides an isolated administrator-only ENE scan.

## Isolated candidate

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
Keep Elgato running when testing the native Stream Deck adapter. Its old Python
compositor must be stopped. Omit `-VirtualOnly` only for the planned hardware test.

Copy private Govee companion settings with `import-govee.py` into a new private
directory, then reference its generated config using the `GoveeBluetooth`
section documented in the driver README. The importer never changes the old
bridge. Configure other devices and strip lengths only after enumeration.

The SDK integration test creates its own temporary settings and never uses this
hardware candidate. Existing SignalRGB layouts and registry are untouched.
# Native capture profiles

`create-native-effect-profiles.py` prepares native preset profiles and optional
named BetterScreenCapture scenes in a new output directory. It copies an existing
profile's controller routes and unrelated plugin state, reads defaults from the
Effects preset specifications, and stores source/scene selection per profile.
It performs no SDK calls or live file modifications. `--scene "Name=UUID"`
creates a Screen Ambience profile following Better's native appearance; raw
screen effects retain their own processing. Optional music template/map arguments
reuse existing audio settings and a dedicated Visual Map route. The independent
profile/routing regressions run with `python tools/room-setup/test_native_capture_profiles.py`.
