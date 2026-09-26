# OpenRGB Room

Development fork of OpenRGB for native device control and image-sized effects.
Based on upstream `0f8f2dccc46576f0a9dac6b70adcbf49901c6b73` (25 September 2026).
The upstream application is already native Qt Widgets. This fork improves its
data paths; replacing Qt with another UI framework is not required for these fixes.

## Delivered source

* Native Windows Govee BLE H6008 realtime and H6159 classic profiles, with bounded
  reconnection, latest-frame delivery and H6159 power-off for black.
* KBHE RAW HID driver, restricted to the real RGB interface, with 82 physical LEDs.
* Unified Alienware monitor driver with classic, authenticated and newer protocol
  profiles; conservative per-model update intervals, including AW3426DW.
* Stream Deck background controller: native C++ client of the existing Elgato
  compositor, preserving actions/icons. It accepts either an ordinary 80×50 matrix
  or a separate full image. **The Elgato Python/Frida compositor is still required.**
* Govee LAN discovery matched by stable MAC, new segment profiles, HYTE zone/single
  updates and NVIDIA monochrome brightness correction.
* Large-device UI: lazy LED list labels, bounded popup work, coalesced preview
  refresh, hidden-page suppression, one color snapshot per paint and reduced
  drawing detail at low zoom.
* [FrameSurface](FrameSurface/README.md): bounded, latest-only native BGRA image
  transport, independent of the legacy SDK LED-count encoding.
* [Generic image outputs and Room SDK7](FrameRouting/README.md): explicit screen
  capabilities, per-output affine transforms, native plugin attachment and image
  packets with 32-bit dimensions. Old LED SDK clients retain their existing format.
* Configurable virtual screens and a bounded native screen preview. The same
  interface serves physical drivers, network peers and plugin-created surfaces;
  wallpaper and AIO transports can be added without rewriting the effects.
* [Effects fork](https://github.com/Fefedu973/OpenRGBEffectsPlugin/tree/room-canvas):
  DXGI capture, configurable Ambient working image/zone sampling, high-resolution
  shaders, native surface publication and bounded preview delivery.
* [Visual Map fork](https://github.com/Fefedu973/OpenRGBVisualMapPlugin/tree/room-surfaces):
  independent scene resolution, generic image routing through virtual devices,
  spatial transforms and a bounded LED grid for legacy clients.

Source coverage is **not** physical confirmation of the new C++ ports. Protocol
fixtures, fake radios, Qt offscreen tests and loopback/shared-memory tests are
separate from a native test against each real device. See the
[device audit](docs/room/native-device-audit.md) and controller test READMEs.
G502 X PLUS and G512 already have upstream HID++ implementations; they have not
been replaced on the basis of an old OpenRGB release's behavior.

## Build on Windows

Use MSVC 2022 x64, a matching Qt kit (tested: Qt 6.8.3 MSVC2022 x64), Windows SDK,
Git and jom. Run from a **Developer PowerShell for VS 2022**:

```powershell
git clone --branch room-integration https://github.com/Fefedu973/OpenRGB.git OpenRGB-Room
git clone --branch room-canvas https://github.com/Fefedu973/OpenRGBEffectsPlugin.git OpenRGB-Effects-Room
git clone --branch room-surfaces https://github.com/Fefedu973/OpenRGBVisualMapPlugin.git OpenRGB-VisualMap-Room
git -C OpenRGB-Effects-Room submodule update --init Dependencies/QCodeEditor Dependencies/SimplexNoise
cd OpenRGB-Room
.\tools\room-build\Build-Room.ps1 -QtKit C:\Qt\6.8.3\msvc2022_64 -Jom C:\Tools\jom.exe -EffectsRoot ..\OpenRGB-Effects-Room -VisualMapRoot ..\OpenRGB-VisualMap-Room -Package
```

The explicit `OPENRGB_ROOM_ROOT` build variable uses the same core headers in both
plugin builds. The optional original OpenRGB submodule is not needed for that command.
No global Qt installation, firmware change or application startup is performed by
the build script. Qt/compiler runtime files in `dist-room` are for local use;
source publication does not imply a redistribution audit of every dependency.

## First native hardware test

Keep the existing working setup until testing each port. Stop SignalRGB and other
RGB hardware owners before allowing OpenRGB detection. For BLE, suspend the old
Govee companion/supervisor; closing SignalRGB alone does not release that radio
connection. Keep the Elgato application and its compositor for background mode.

The portable candidate uses a separate `--config` directory. It does not import or
overwrite the normal OpenRGB profile or SignalRGB registry. The launcher checks
for the known conflicting processes and refuses to kill them automatically.
Configure physical strip lengths/channel order only after enumerating the device.

* [Govee BLE setup](Controllers/GoveeBluetoothController/README.md)
* [Stream Deck setup](tests/room-streamdeck/README.md)
* [Wallpaper compatibility and image design](docs/room/wallpaper-and-native-canvas.md)
* [Large-device changes and validation](docs/room/large-devices.md)
* [Canvas export](tools/room-canvas/README.md)

`tools/room-setup/import-govee.py` can copy the existing private companion settings
to the native profile format. Keep its output in `private/`; addresses, session
tokens and communication keys are never part of the public repository.

The Full Scale canvas export preserves positions, rotations, scaling, components
and LED order. Actual OpenRGB identity/LED bindings still require hardware
enumeration; the exporter deliberately leaves unverified bindings empty. The
Screen Ambience scene and a native wallpaper renderer are not delivered yet.

This is a development branch, not an installed replacement or an autostart
migration. Hardware testing and native wallpaper/GPU-texture integration remain
separate steps. Existing upstream licenses and third-party notices apply.

`--virtual-only --noautoconnect` starts configured `VirtualScreens` without a
hardware scan. It is useful for SDK/image development alongside the normal RGB
application. It does not create an animated desktop wallpaper by itself.
