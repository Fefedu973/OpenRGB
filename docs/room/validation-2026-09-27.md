# Room integration validation — 27 September 2026

These are software tests, performed without taking control of the user's RGB
hardware. The development branches are not a completed hardware migration.

| Area | Verified result |
|---|---|
| Build | Core executable, Effects DLL and Visual Map DLL compile with MSVC2022 x64 / Qt6.8.3 |
| Image codec | Explicit endian/header bytes, malformed sizes/NaN/truncation, transformations, 800×600 input and 4000 deterministic mutations |
| Real SDK server/client | Python fixture and actual C++ NetworkClient send 800×600 through TCP to two native virtual outputs (800×600 and 321×197); shared-memory pixels match, including mirror and brightness |
| Old SDK peers | SDK4 enumeration; SDK6 controller description identical to SDK7 for tested virtual devices; C++ client sends no image packets to SDK6 or a peer without the capability flag, including fragmented responses |
| Network interruption | Four abandoned large image bodies release admission capacity; later valid frames still work; client retirement drops image capability and stops within its bound |
| Plugin boundary | Both real DLLs load through QPluginLoader against an API5 test host; Visual Map routes the same immutable 800×600 frame through the real core wrapper to a synthetic sink with the expected affine; preview and unload pass |
| Plugin lifecycle | 17 Qt event-queue and 14 NO_GUI assertions: cancellation, deletion, registration supersession, stale identity, reentrance and API close |
| Virtual screens | 57 synthetic assertions, including 25 immediate start/stop cycles, limits, TTL and frame ownership |
| Visual Map | Real routing/wrapper classes tested for spatial mapping, legacy weighting, holes/non-affine fallback, cycles, mailbox, TTL, detach and destruction |
| Effects | Ambient routing/sampling tests, shader canvas/mailbox tests and capture rotation/pitch/pool tests; no real desktop/audio capture in the DLL smoke test |
| Large LED UI | Actual 800×600 / 480000-LED Qt offscreen widget: median paint about24ms; lazy 500000-entry chooser constructs no per-LED labels up front |
| Native transports | Govee fake-radio and crypto tests; Alienware packet fixtures; KBHE fake HID; Stream Deck loopback HTTP/shared surfaces; LAN discovery and NVIDIA brightness tests |

The SDK fixture is `FrameRouting/tests/sdk_integration.py`; native client and
DLL harnesses are under `tests/room-network-images` and `tests/room-plugin-images`.
Each subsystem README supplies its build/run command and the boundary being
mocked. Performance figures are synthetic measurements on this PC, not hardware
refresh rates or guarantees for every GPU/shader.

Remaining validation: optical tests of the new native drivers, real capture/GPU
throughput, device identity/LED bindings for the imported room canvas, and the
full user migration. The existing Elgato compositor remains required. A native
wallpaper renderer, GPU texture sharing, HDR handling and the deferred Screen
Ambience layout are not included in this delivery.
