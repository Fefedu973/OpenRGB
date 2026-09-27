# Nonblocking controller color frames

This test compiles the production `RGBController.cpp` and uses its existing
device worker. Two synthetic transports replace hardware I/O; no device is
opened and no application configuration is read or changed.

Run `build-test.cmd` from an x64 MSVC Developer Command Prompt with `QT_ROOT`
pointing to a Qt MSVC installation. The executable reports its assertions and
measured submission time. The 27 September 2026 run passed 82 assertions:

- One simulated device holds its production shared `AccessMutex` through 400 ms
  of I/O while the other completes 12 updates. Maximum measured submission time
  was 0.0026 ms on that run; this is a synthetic result, not a device FPS claim.
- The slow device receives the initial frame followed by only the latest frame.
  Replaced allocations are released and there is no history to replay.
- A partial frame combines distinct segments; unlisted LEDs remain unchanged
  and duplicate indexes retain submission order.
- Resize invalidates a queued frame even when its indexes would still fit.
  Invalid indexes cannot partially modify colors; size and lease are bounded.
- JSON and SDK 7 metadata round-trips retain colors and change the topology
  token, with storage reconstruction kept inside the existing exclusive lock.
- Adding/removing segments and configuring a zone invalidate the old route
  token even if the color-buffer length is unchanged.
- Expiration discards a delayed frame. Shutdown rejects further submissions and
  purges pending work before joining the original device worker.

## Interface and ownership

`FrameRouting/RGBControllerColorFrameInterface.h` is an optional secondary C++
interface. It does not add methods to the PluginAPI 5 controller interface.
Callers use `dynamic_cast` and retain their legacy path only when the capability
is absent or explicitly unsupported. `Busy`, `Stale`, and `Invalid` must never
trigger blocking per-LED fallback.

Each controller retains one immutable pending `ColorFrame`, at most 1,048,576
index/color pairs (8 MiB). Submit uses a separate try-lock mailbox, performs no
I/O and invokes no callbacks. The existing device worker validates and applies
the latest frame under its exclusive color lock, then uses the unchanged shared
lock for transport I/O. No worker or transport lock was removed or added.

Coalescing is **per controller**, not per zone or producer. A producer must group
all segments it controls into one frame. A later accepted frame replaces the
entire pending frame; omitted LEDs retain their last applied colors. Multiple
producers are last-writer-wins and are not an implicit blending mechanism.

Capture the topology token when routes are built or rebound, rather than on each
render. Resize and color-buffer recreation invalidate pending work. A frame may
remain pending for its 100–5000 ms lease (default 1000 ms); accepted means queued,
not delivered. Manual legacy color writes and profile color imports discard a
pending frame. Callers must keep the controller alive for the call, as required
by the existing controller API.

Segment changes also invalidate the token. If an integration does not receive a
controller-list update for such a change, it must explicitly rebuild its routes
before resuming; repeatedly reading a fresh token without rebuilding indexes
would defeat the guard. The current Visual Map integration fails closed until
that rebind rather than replaying obsolete routes.

The native runtime baseline independently identified transport lock contention
in NVIDIA, Corsair, RAM and ASUS controllers. This test proves isolation and
bounded coalescing only; comparative live cadence is a separate measurement.
