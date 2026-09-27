# Isolating slow lighting outputs

Visual Map's image router previously called `SetColor` synchronously for each
sample. A controller's device worker held the shared color lock throughout its
USB, SMBus or driver call, while `SetColor` required that same lock exclusively.
One slow output could therefore delay the next image for every other output.

The optional `RGBControllerColorFrameInterface` lets the router submit one
indexed color batch per physical controller, combining all its selected zones
and segments. Submission uses a separate nonblocking mailbox. There is one
pending frame, replaced by newer input, not a growing queue. The existing device
worker applies it and performs the driver's I/O; no extra worker or server is
introduced. Slow hardware retains its own limits without imposing them on the
whole room.

The worker validates every index before changing any color. Topology tokens
reject frames built before a resize or reconfiguration; leases discard expired
input. Shutdown closes admission and discards pending work. A rejected batch
never falls back to blocking writes. Hosts without the optional interface keep
the legacy route; the published PluginAPI 5 interface is unchanged.

Visual Map still needs a route rebuild after a controller's topology changes.
Where a segment edit does not emit a device-list update, reselect the map to
rebuild it; stale routes remain stopped instead of writing to old indices.
This change affects image-to-LED routing; unrelated third-party effects that
write colors synchronously do not automatically gain the mailbox behavior.

Tests use the real controller worker with a simulated 400 ms driver, verify a
fast neighbor continues, and prove only the newest pending batch survives.
They also cover segment coalescing, atomic index validation, resize, expiration,
shutdown and the secondary interface across the real plugin DLL. See
`tests/room-color-frames` and `tests/room-plugin-images`.

The existing Wallpaper Engine SDK bridge is retained. Profiling showed its
512-color matrix writes were a negligible part of the routing delay; no wallpaper
fork or image-protocol migration is required for this fix.
