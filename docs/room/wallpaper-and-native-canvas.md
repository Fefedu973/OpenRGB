# Wallpaper, images and the native canvas

Audit: 27 September 2026. Source: Delido/signalrgb-wallpaper commit
`2d1099eeaf1f59c1693e22503ba95d7a50fc6603`, MIT licensed.

## Existing compatibility: yes

`wallpaper_bridge/openrgb_server.py` already exposes one virtual matrix device
per screen through an OpenRGB SDK server, default loopback port6743. This is
separate from its OpenRGB output client. An OpenRGB client connects to this
server, enumerates the matrices and sends colors to them. SignalRGB is not
required in this route. The SDK server negotiates version4; OpenRGB1.0 supports
older versions through negotiation. Source code currently declares app2.5.0;
the repository README shown by the web index described older features.

Local protocol test passed: version6 request negotiated4; one32×16 controller
enumerated; all512 RGB values in a test frame arrived unchanged at the wallpaper
callback. No OpenRGB GUI or real desktop renderer was started for this test.

Existing user configuration already has `openrgbSdkServer`, disabled, and its
screen sources still point to SignalRGB. It has not been modified. To switch
later: enable that server in Integrations, set the intended screen source to
OpenRGB SDK, then connect OpenRGB's SDK client to127.0.0.1:6743. This is a client
connection, not OpenRGB's own server on6742. Keep only one source per screen.

## Limits reproduced

| Matrix | Cells | Descriptor result |
|---|---:|---|
|32×16|512|Valid canonical SDK4 layout|
|80×50|4000|Valid canonical SDK4 layout|
|160×100|16000|Valid canonical SDK4 layout|
|320×200|64000|Wrong declared matrix byte length:65535 versus256008|
|256×256|65536|Python struct.pack fails on the16-bit LED count|

OpenRGB's color and LED counts still use16-bit values, including SDK6. Matrix
byte lengths also use16bits:8+4×cells fits through16381cells. The C++ parser
uses this length mainly as a presence flag and may read larger maps from their
dimensions, so16381 is an interoperability limit of the canonical encoding,
not proof that every OpenRGB code path rejects every larger map. The current
wallpaper clamp allows256×256 despite its serialization failure. Increasing
that clamp alone cannot deliver full-resolution video.

The isolated test script/result are retained in the local research outputs;
the Stream Deck tests include a separate discussion in
`tests/room-streamdeck/HIGH-RESOLUTION.md`.

## Implemented first stage and remaining native design

The Room forks now implement a first stage: bounded native BGRA shared-memory
surfaces, a Stream Deck surface reader, DXGI capture with CPU readback in the
Effects plugin, Ambient working-canvas/UV sampling, and shader image publication.
See [large-devices.md](large-devices.md) and [FrameSurface](../../FrameSurface/README.md).
This stage is built around immutable CPU images; GPU texture sharing and a native
desktop wallpaper renderer remain future work. The target architecture below
distinguishes those next steps from the code already delivered.

Keep OpenRGB for hardware drivers and optional coarse virtual matrices. Build
one image-oriented canvas engine rather than representing a desktop image as
millions of LED objects. The engine owns a GPU texture and a canonical scene
coordinate system; Full Scale placements remain independent of texture size.

- Capture: Windows Graphics Capture or DXGI Desktop Duplication, with explicit
  monitor selection and crop. Do not infer monitor geometry from LED counts.
- Composition: Direct3D11/Direct2D textures for capture, effects and image masks.
  Use double/triple buffering and latest-frame delivery, not an unbounded queue.
- Physical LEDs: sample the texture at the imported per-LED world coordinates;
  send small color buffers to native OpenRGB drivers at each device's own rate.
- Stream Deck: crop/resample the image into its existing15 tiles and feed the
  compositor hook. Preserve Elgato's action/icon layers. The current80×50
  OpenRGB controller remains an effects-compatible fallback, not the final
  maximum image resolution.
- Wallpaper: render the texture directly with a native desktop surface. A
  standalone process isolates Explorer/restart handling; shared D3D textures
  avoid encoding every frame into JSON or Python RGB arrays.
- Interface: native Qt widgets/plugin UI is the smallest integration step;
  WinUI3 is a separate frontend process with a command/state API. Neither needs
  to implement the SignalRGB plugin runtime.

A native OpenRGB plugin can register virtual controllers, but the ordinary SDK
does not become a general image API as a result. A dedicated texture/frame API
is required for high-resolution output. Reusing the current wallpaper app first
provides a working migration route; a rewrite would replace Python plus the
HTML wallpaper/render host, not merely swap its configuration page.

No fork of Delido's project was needed to establish virtual-device support.
No claim of a completed native wallpaper renderer or complete GPU image canvas is
made by this audit. Performance gains need CPU/GPU/frame-latency measurements
on the same scene, not an assumption that a native UI alone solves them.

Primary sources:
- https://github.com/Delido/signalrgb-wallpaper/blob/2d1099eeaf1f59c1693e22503ba95d7a50fc6603/wallpaper_bridge/openrgb_server.py
- https://github.com/Delido/signalrgb-wallpaper/blob/2d1099eeaf1f59c1693e22503ba95d7a50fc6603/wallpaper_bridge/bridge.py
- OpenRGB `RGBController/RGBController.cpp`: GetColorDescriptionData,
  SetColorDescription, GetZoneDescriptionData and SetZoneDescription.
- OpenRGB `OpenRGBPluginInterface.h`: RegisterVirtualRGBController.
