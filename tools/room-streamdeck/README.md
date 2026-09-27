# Stream Deck profile selection without another bridge

`ProfileSelect.cpp` builds a small Windows GUI-subsystem EXE. It opens one
connection to `127.0.0.1:6742`, negotiates SDK version 6 or newer, requests the
named profile through packet 152, checks the ACK and verifies the active name
through packet 156. It then exits. It performs no device scan, reads no private
credentials and has no daemon, autostart or lighting-output loop.

From an x64 MSVC developer prompt:

```text
cl /nologo /EHsc /std:c++17 /MT /O2 tools\room-streamdeck\ProfileSelect.cpp /Fe:ProfileSelect.exe /link /SUBSYSTEM:WINDOWS
ProfileSelect.exe --profile "Full Blanc"
python tools/room-streamdeck/test_profile_select.py --exe ABSOLUTE_PATH_TO_EXE
```

Do not run the example profile command during an unrelated hardware test.
The test script exclusively starts temporary fake loopback servers on random
ports. The program allows `--port` for those tests, but never a remote host.
Its total deadline defaults to 30 seconds (`--timeout-ms 100..30000`). Invalid
responses, missing profiles, mismatched active profile and timeouts return a
nonzero exit code. A named mutex prevents simultaneous loads from repeated
button presses. No implicit command retry occurs.

For Elgato, create a Windows shortcut with TargetPath pointing to this EXE and
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
