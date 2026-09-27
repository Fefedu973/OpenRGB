# General Settings and last-session regression tests

Run `build-test.cmd` from an x64 MSVC developer prompt with `QT_ROOT` pointing
to a Qt 6 MSVC installation. The executable uses `QT_QPA_PLATFORM=offscreen`.
No application configuration, SDK server or hardware is opened.

The harness compiles the production `OpenRGBSettingsPage` and
`OpenRGBDynamicSettingsWidget` with synthetic manager data. It checks GUI-thread
ownership and visible content after a worker requests a schema refresh, queued
receiver deletion, profile-list refresh without writes, empty/missing profile
selection, and a genuine user edit that still persists once.

The production `LastSessionCheckpoint` and `ProfileLoadState` are also exercised:
atomic file round-trip through a temporary directory, unchanged JSON deduplication,
loads in progress and loads completed during serialization, remote-load pairing,
empty/transient states, invalid JSON/version, controller-data exclusion, and failed
file writes. This does not substitute for a packaged application restart test.

Validated on Windows x64, Qt 6.8.3 / MSVC 2022: **130 assertions passed**.
Before the fix, the real nine-section schema reproduction lost all 10 group boxes
after a worker refresh; updating its profile lists caused 13 unnecessary settings
writes. Both symptoms are covered by the synthetic public regression fixture.
