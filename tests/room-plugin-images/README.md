# Real DLL / secondary image interface integration

This harness loads the built Visual Map and Effects plugins with `QPluginLoader` in a real Qt 6 application running with the **offscreen** platform. It compiles the actual core `RGBController`, `RGBController_Virtual` and image interfaces. The host Plugin API5 service implementation is a test double: it supplies only synthetic controllers, inert settings and an isolated `QTemporaryDir`. It is not a test of the complete production PluginManager or its asynchronous registration lifecycle.

No detector, desktop capture, audio capture, USB, Bluetooth or physical output is started. The Effects plugin is loaded and unloaded with no effect or saved profile. The Visual Map plugin receives an explicitly generated map and image; its only destination is an in-process synthetic image sink.

```powershell
$env:QT_ROOT = '<Qt 6 MSVC directory>'
./tests/room-plugin-images/Build-Tests.cmd
$env:PATH = "$env:QT_ROOT\bin;$env:PATH"
./tests/room-plugin-images/.build/plugin-image-test.exe '<VisualMap DLL>' '<Effects DLL>'
```

The test proves:

- Both real DLLs load using the unchanged Plugin API5 interface.
- The Visual Map DLL can query the optional image API with `dynamic_cast` on an API object instantiated in the executable.
- The real core virtual-controller wrapper forwards calls across the DLL boundary to Visual Map's image sink.
- An immutable 800×600 BGRA frame passes through the map to the synthetic destination with the exact same `shared_ptr` and affine mapping `(origin=.1,.1; U=.4,0; V=0,.3)`. It is not reduced to its compatibility LED matrix.
- The native route does not also submit LED updates, and the same frame is available through the optional preview API.
- Two synthetic segments belonging to zone1 load as distinct members and receive independent position/brightness samples, through zone+segment LED offsets; the preceding zone0 stays untouched.
- Both DLLs unload cleanly and the map detaches and deletes its virtual wrapper before the test API is destroyed.

The 2026-09-27 run passed with Visual Map SHA256 `6467c13628ec66ab0f430d6f68152eb9781b07371a2f26f5897068dac17c7e6d`; the synthetic 800×600 map exposed 1,508 compatibility LEDs. The exact LED count is diagnostic only; the assertion is that the native image is 800×600 and the compatibility matrix remains bounded. This does not benchmark a complete OpenRGB application or validate physical displays.

The later affine/segment validation passed with Visual Map SHA256
`55c464abd77d85e1984b614b83699ce243adaefdd0c441afc3377d078bb42aa6`:
two real plugin DLLs, 1,524 compatibility LEDs, preview0ms into its1,500ms lease,
the native shared frame and both segment outputs verified, clean unload.

The API test double is adapted from `OpenRGB-VisualMap-Room/tests/room-image-routing/FakeAPI.h`. All test source is GPL-2.0-or-later. `.build/` is excluded from version control.

The preview assertion runs immediately after submission, before waiting for the
consumer, and reports `previewElapsedMs`/`previewLeaseMs`. A stalled test process
that exceeds its 1,500ms lease is reported explicitly as a scheduling failure;
the production lease is unchanged. The former order could wait 2,500ms and then
incorrectly require an already expired preview to remain available.
