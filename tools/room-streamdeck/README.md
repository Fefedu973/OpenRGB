# Stream Deck profile selection without another bridge

`ProfileSelect.cpp` builds a small Windows GUI-subsystem EXE. It opens one
connection to `127.0.0.1:6742`, negotiates SDK version 6 or newer, requests the
named profile through packet 152, checks the ACK and verifies the active name
through packet 156. It then exits. It performs no device scan, reads no private
credentials and has no daemon, autostart or lighting-output loop.

From an x64 MSVC developer PowerShell:

```text
.\tools\room-streamdeck\Build-ProfileSelect.ps1
.\build\room-streamdeck\ProfileSelect.exe --profile "Full Blanc"
python tools/room-streamdeck/test_profile_select.py --exe build/room-streamdeck/ProfileSelect.exe
```

`tools/room-build/Build-Room.ps1 -Package` (also `-PackageExisting`) builds this
helper using the current x64 MSVC environment and copies it to the canonical
portable location `dist-room/ProfileSelect.exe`. It records the binary and source
SHA-256 in `BUILD-INFO.json` and ships `ProfileSelect-BUILD-INFO.json`. The helper
uses the static MSVC runtime; no Qt runtime or Python is needed when pressing a
button. Build and packaging do not execute it. `/Brepro` makes repeated builds
with the same compiler and inputs reproducible.

Do not run the example profile command during an unrelated hardware test.
The test script exclusively starts temporary fake loopback servers on random
ports. The program allows `--port` for those tests, but never a remote host.
Its total deadline defaults to 30 seconds (`--timeout-ms 100..30000`). Invalid
responses, missing profiles, mismatched active profile and timeouts return a
nonzero exit code. A named mutex prevents simultaneous loads from repeated
button presses. No implicit command retry occurs.

For Elgato, create a Windows shortcut with TargetPath pointing to the absolute
path of `dist-room/ProfileSelect.exe` and
Arguments `--profile "PROFILE NAME"`. Use the native Stream Deck **System →
Open** action (`com.elgato.streamdeck.system.open`) to open that shortcut. This
avoids shell quoting and does not start another OpenRGB process. The EXE has no
console window. A successful SDK acknowledgement confirms profile selection,
not the perceived state of every light.

Changes to Stream Deck profiles require a separate, explicit installation step:
first save a complete backup of the affected `.sdProfile`, then shut down Elgato
normally so it cannot overwrite edited manifests. Replace only the reviewed
folder page and add the new shortcut actions. Keep the original parent-folder
action and its image. Restore the saved profile tree with Elgato closed to undo.
This source and test contain no live profile IDs, device addresses or credentials.

`extend_effects_menu.py` prepares an additional submenu containing up to 14
native effect profiles. It requires a free parent key and preserves every
existing action. Pass `--profile-dir`, `--parent-page`, `--profiles-dir`,
`--shortcuts`, `--title` and an `--output` directory outside the live profile.
The proposal records the original parent manifest hash; check that hash again
before installation so a newer user edit is never overwritten. Preparation does
not create shortcuts or modify the live profile.

For background diagnostics, `python tools/room-sdk/streamdeck_status.py
--samples 3` reads the cached native compositor status through SDK7. It does not
send images, select profiles or modify configuration. An increasing ACK count
proves compositor activity, not visible LCD output: empty-key cache failures
must also be checked on the device. `--self-test` validates descriptor parsing
offline. Replacing the native compositor DLL requires OpenRGB to exit fully;
when updating Elgato profiles too, quit OpenRGB before quitting Elgato to allow
the old background to restore normally.
