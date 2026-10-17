# Shutdown regression checks

Run from an x64 MSVC developer shell. These tests do not detect devices, open
hardware, load plugins, or control an existing OpenRGB instance.

```powershell
python tests/room-shutdown/test_final_frame.py
python tests/room-shutdown/test_gui_teardown.py --qt C:/path/to/Qt/6.8.3/msvc2022_64
```

`test_final_frame.py` compiles the production final-frame, control-toggle and
device-configuration bodies with inert sinks. It checks final black versus
restoration and preserves the disabled-control policy.

`test_gui_teardown.py` extracts the production dialog `closeEvent`,
`ClearDevicesList`, and the entry guard of `UpdateDevicesList`. It compiles them
with real Qt `QWidget`/`QTabWidget` instances and fake controllers/plugin unload.
The test verifies that three visible device pages, two hidden pages and three
information pages are destroyed before plugin-owned controllers, while plugin
containers remain owned by Qt. It also checks an already queued list-update
attempt during unload, idempotent clearing and a second close notification.
The rest of device detection/list population is deliberately not exercised.

The regression was observed on 17 October 2026 in two shutdown dumps, before and
after an unrelated BLE change. Both failed in `OpenRGBDevicePage` destruction,
while unregistering a callback through a controller that had already been
destroyed. The dialog had disconnected resource-list notifications before
unloading plugins, so virtual-controller deletion could no longer remove its
device pages. Removing all controller views before plugin unload restores the
required lifetime order; it does not skip callback deregistration or defer
cleanup until after the controller is gone.

Offline Qt validation passed. A separate normal exit of the installed build on
17 October 2026 reached device cleanup and confirmed power-OFF readback for the
H6159 and all three H6008 bulbs. The process exited and Windows recorded no new
Application Error/Windows Error Reporting crash event. The exit-code observer
returned null, so this is not a claim of an independently verified exit code 0,
nor proof that every hardware driver completed all its cleanup work.
