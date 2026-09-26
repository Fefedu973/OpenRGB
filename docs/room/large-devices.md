# Large LED devices and image surfaces

The application UI and the image transport solve different constraints.

## Application UI

`LazyLEDListModel` retains the existing LED order and generates display labels on
demand. Selecting all LEDs, a zone or a segment no longer creates a QString for
every LED. The combo uses a list popup with uniform row sizes and a single layout
pass: Qt's default menu-style popup otherwise asks for all 500,000 names before
showing even a short viewport. The proxy style owns its own base style.

Offscreen Qt 6.8.3/MSVC release test on this workstation, 500,000 lazy rows:

| Operation | Measured behavior |
|---|---|
| Original menu-style popup over the lazy model | ~3.58 s, ~500,004 labels evaluated |
| Corrected list popup | ~22–24 ms, 13 labels evaluated |
| Model initialization | ~0.06 ms, zero labels evaluated |
| Jump to final row | Final row visible; Home/End tested |

These measurements test an actual QComboBox offscreen, not USB throughput, a full
device-page load or SDK serialization. Qt still allocates row-layout bookkeeping
(about 5.3 MB in this popup run); this is not zero-memory virtualization.

LED update callbacks set one atomic dirty flag. A GUI timer consumes it at most
once every 33 ms when the preview is visible. This prevents a fast driver from
building an event queue of repaint notifications, and hidden pages do not repaint
for every frame. Non-color controller updates retain their original event route.

The preview takes one locked color snapshot, reuses its storage, clips drawing
outside the paint region and omits outlines/text when cells are too small. It no
longer takes three color locks per drawn LED or creates labels for every cell in
a huge matrix. Geometry and selection remain per-LED. For dense previews, small
opaque cells are rasterized into one bounded image of the visible region, then
drawn once. Mixed large/small cells and selection retain their drawing order. An
actual480,000-LED DeviceView offscreen test improved median repaint from about82 ms
to24 ms, with functional hit-testing for subpixel cells. This is a UI test, not
an SDK/device-throughput measurement.

## Why an 800×600 screen is not a 480,000-LED SDK device

An image carries width, height, stride, pixels and freshness. Physical LEDs have
positions and sample that image. FrameSurface makes that distinction explicit.
Stream Deck receives a high-resolution image without updating its 4,000-LED
compatibility matrix. A future native wallpaper renderer can consume the same
surface directly.

The legacy OpenRGB SDK still has 16-bit LED/color counts. The negotiated Room SDK7
image extension carries32-bit image dimensions and payload lengths; existing LED
packets remain unchanged. The local500,000-row UI test does **not** mean that the
old LED protocol can serialize such a controller.
Matrix descriptor length compatibility is detailed in the wallpaper audit.

## Effects

Ambient uses DXGI Desktop Duplication on Windows, with bounded GDI fallback,
immutable QImages and a working canvas (default 800×600). It prepares one cropped
image and samples actual LED positions/zone UV rectangles instead of resizing
that whole image separately for every device. Capture and output rates are
independent. Image publishers use latest-frame delivery and stale-frame expiry.

Shaders accept dimensions through 4096 with an 8,388,608-pixel total budget;
800×600 and 3840×2160 fit, 4096×4096 does not. The preview is bounded to 640×360 at
about 15 Hz and has no work while hidden. LED sampling avoids whole-image copies
and resizes per zone. Shader image publication includes brightness/temperature/
tint without modifying the source frame.

This first implementation uses CPU BGRA shared memory. DXGI still reads back a
staging texture; shaders still read back their OpenGL FBO. It is **not** a GPU
zero-copy design. GPU shared textures, HDR tone mapping, native wallpaper output
and a complete visual room editor are future work, not performance claims.

Tests are reproducible under `tests/room-large-device-ui`, `FrameSurface/tests`,
`tests/room-streamdeck` and the Effects fork's `tests/room-ambient`,
`tests/room-capture`, `tests/room-shaders`. See each README for its tested scope.
