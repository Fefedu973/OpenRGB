# Native image outputs and Room SDK 7

This is a general image capability, not a Stream Deck packet format. Any driver
may expose any number of its zones through `RGBControllerImageInterface`.
The shared frame is immutable BGRA8 with explicit width, height and stride;
an affine transform maps each output's local UV coordinates into the scene.
Transform brightness is an additional per-zone gain, separate from source image
adjustments. Screen geometry, canvas coordinates and legacy LED density are
independent. There is no device-name or vendor switch in the effects router.

`GetImageOutput` describes capability; `SubmitImage` accepts owned shared frames
without hardware I/O and retains only the latest value. `Busy`/`Invalid` never
mean "try to write its LED matrix instead". Leases expire after 100–5000 ms.
Drivers retain their transport limits and sampling/output logic; declaring a
screen capability cannot make an undocumented AIO's USB protocol work by itself.

The generic application preview queries `GetImagePreview`, which is read-only
and optional. It samples directly to a bounded thumbnail, only while visible.
Native controllers and network controllers implement the same interface.

## Plugins

Existing plugin API5 and RGBControllerInterface vtables are unchanged. Effects
can query the optional image interface on any RGBControllerInterface pointer.
Plugins that create virtual devices query the additional `room_image::PluginAPI`
interface from their API pointer. `AttachImageInterface` connects their sink to
the core virtual-controller wrapper. Attach before registering the device, and
detach before destroying the sink/unloading the plugin. Detach synchronizes with
active image calls. Unmodified API5 plugins continue using LED data.

## SDK extension identity

The fork negotiates protocol **7**, plus server flag **bit29** and the **RIMG
schema1** marker. These are development-fork extensions, **not allocations
approved by upstream OpenRGB**. They must be reconciled before an upstream merge.
Clients must check all three rather than assuming a future upstream version7
means this extension. Old peers negotiate their older version and never receive
image packets. Existing LED descriptors/packets are byte-for-byte unchanged.

The ordinary 16-byte ORGB envelope still contains magic, device ID, packet ID
and payload length (all numeric fields little-endian). Since SDK6, device IDs
are stable server IDs rather than indexes. New packet IDs:

| Packet | Value | Payload |
|---|---:|---|
| Request image outputs | `0x524D0001` | Empty request; descriptor reply below |
| Update image | `0x524D0002` | Frame header + BGRA bytes |

Descriptor reply: uint32 magic `0x474D4952` (RIMG), uint32 schema1, uint32 count;
then `count` records of uint32 zone, width, height, max_fps. Maximum4096 records.
Dimensions describe the output's preferred/native resolution, not a required
input size. No matching image capability gives a successful empty list.

Frame header (100 bytes): eight uint32 values (magic, schema, zone, width, height,
stride, pixel_format=1, lease_ms), uint64 sequence, seven IEEE754 float64 values
(origin_x, origin_y, u_x, u_y, v_x, v_y, brightness), uint32 payload_bytes.
Exactly stride×height bytes follow. BGRA8 is opaque sRGB, top-left origin.
Maximum64 MiB pixel payload and dimensions1..16384; stride≥width×4. NaN/Inf,
overflow, unsupported format/schema, truncation and trailing data are rejected.

Only a negotiated image-update packet gets the larger packet bound; ordinary
SDK messages retain their existing 8 MiB limit. The server submits directly to
the sink's nonblocking latest-frame path, rather than queuing large images on
the legacy LED worker. The ordinary ACK reports accepted/unsupported/invalid;
new status6 means busy. Accepted means queued, **not** optically displayed.
Incomplete TCP image bodies release their buffer on every exit; an aggregate
128 MiB receive budget and a 10-second body deadline bound stalled peers.

Raw800×600 BGRA is1.92 MB per frame (~115 MB/s at60 fps, before transport overhead).
Use FrameSurface locally to avoid TCP copies where possible. This first schema
does not promise compression, zero-copy GPU textures or unrestricted frame rates.

## Validation

`FrameRouting/tests/test_protocol.cpp` is a standalone C++17 test: explicit wire
bytes, padding, malformed headers, truncation, NaN, multi-output descriptors,
800×600 (>65535 pixels), transforms and4000 deterministic packet mutations.
Compile with any C++17 compiler; no Qt or physical device is needed. Full client/
server integration is exercised separately against configured virtual screens.
