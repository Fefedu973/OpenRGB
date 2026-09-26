# Configurable virtual image screens

This controller is a generic image-output adapter and a hardware-free reference
implementation of `room_image::RGBControllerImageInterface`. Each configured
output has its own native resolution, identity, frame cadence and local image
channel. It does not detect or control a physical monitor, set the desktop
wallpaper, capture the screen, or start a hardware/network connection.

On Windows it publishes the mapped native image through `FrameSurface` shared
memory. A separate application can read that channel and display it as a
wallpaper or on another screen. On other platforms the in-process preview and
image interface remain available, but this implementation does not claim an
equivalent shared-memory publisher. The public secondary interface also permits
future AIO/display adapters without adding device-specific branches to effects.

## Local configuration

Merge the `VirtualScreens` section of `config.example.json` into the local
OpenRGB settings and explicitly enable it. It is disabled when the section is
absent or `enabled` is false. This section is registered as local-only and is
not a remote SDK configuration endpoint. No private key, account, address or
credential is required.

Each entry accepts:

| Field | Meaning and limits |
|---|---|
| `id` | Stable unique ID; 1–64 ASCII letters, digits, hyphens or underscores. Controller serial is `virtual-screen:<id>`. |
| `name` | Display name, 1–80 UTF-8 bytes without control characters. |
| `channel` | Unique FrameSurface channel with the same syntax as ID. |
| `width`, `height` | Native output pixels, 1–4096 each; defaults 800×600. |
| `fps` | Output cadence 1–60; default 30. This is a cap, not a performance guarantee. |
| `compatibility_width`, `compatibility_height` | Ordinary LED matrix, 2–127 each; defaults 32×18. Independent of native resolution. |
| `enabled` | Optional entry switch; defaults true once the overall section is enabled. |

At most 16 outputs are accepted. Their combined native BGRA output buffers may
occupy at most 64 MiB. The compatibility matrix stays below the legacy SDK's
16-bit matrix-description limit: at most 16381 cells, and the per-dimension
127 bound currently limits a square to 16129. It provides LED-only clients and
layout editors with a manageable geometric grid. An 800×600 screen therefore
has 576 compatibility LEDs by default, not 480000.

Native submissions retain shared immutable frame ownership. Distinct incoming
pixel allocations admitted by all VirtualScreen controllers together are
bounded to 64 MiB and 256 live allocations; identical shared buffers count
once. Admission uses a try-lock and returns Busy when the budget is occupied.
The aggregate output buffer and input budget are separate: a fully occupied
configuration can retain roughly 128 MiB of pixels, plus bounded metadata and
the FrameSurface mappings/readers. Existing external references can keep an
input allocation charged until they release it.

## Routing and lifecycle

Zone 0 advertises the native resolution and cadence. `SubmitImage` accepts
opaque BGRA frames, an affine mapping including per-zone brightness and a
100–5000 ms lease. It is nonblocking and keeps only the latest accepted frame;
newer frames replace pending older frames. Identical frame/mapping submissions
renew the lease without regenerating pixels. Crops, rotations, mirror flips,
out-of-bounds black and brightness are handled through the shared generic
sampler. No shader/effect checks a screen name or model.

The worker samples to the output's configured native size at its own cadence.
It holds no controller lock during the pixel loop, uses a bounded publisher
wait, and checks stop once per row. A static active image is republished every
500 ms. A channel owned by another writer is retried once per second. Readers
can reconnect without restarting the controller.

Ordinary LED writes are suppressed during an image lease. After expiration,
the previous compatibility image is used if available; otherwise the output
publishes black once and becomes idle. A later LED update or native submission
resumes it. Stop joins the worker and closes the channel; it does not terminate
another process or touch hardware. `GetImagePreview` returns the current shared
source frame and mapping without copying the full image. Output descriptor
dimensions remain the preferred display size.

## Safe integration mode

The detector entry point is `DetectVirtualScreenControllers()` declared in
`VirtualScreenController.h`. The host's `--virtual-only` mode calls only this
detector and skips normal hardware detection, rescans and SDK autoconnection.
The normal detector also returns no controllers unless configured explicitly.
Use an isolated settings directory for tests so normal profiles are preserved.

## Offline tests and channel probe

From an x64 MSVC developer prompt:

```bat
set VIRTUAL_SCREEN_TEST_OUTDIR=C:\Temp\OpenRGB-Virtual-Screen-tests
Controllers\VirtualScreenController\tests\run-msvc.cmd
```

The runner builds and runs synthetic configuration, mapping, brightness,
shared-memory, lease, compatibility fallback, latest-frame replacement,
stop/reconnect tests, and compiles the RGBController/detector integration.
It never launches OpenRGB or a physical-device connection.

It also builds the bounded channel probe:

```bat
C:\Temp\OpenRGB-Virtual-Screen-tests\read_surface.exe room-screen-one 5000
```

The probe waits at most five seconds for a fresh frame and emits one JSON
object containing width, height, stride, sequence, generation, byte count and
RGBA samples at the four corners and center. Exit 0 confirms an actual readable
image, 1 means timeout, and 2 means invalid arguments. This allows an SDK test
to verify pixels end-to-end rather than accepting a server ACK as proof of
image delivery. The probe uses only local shared memory.

This driver and its tests are GPL-2.0-or-later and include no vendor binaries,
decompiled implementation, private profile or hardware credential.
