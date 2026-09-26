# Native network image client tests

These tests use the production `NetworkClient` and `RGBController_Network` classes. They do not initialize hardware detection or send USB/Bluetooth commands. The server fixture must use `--virtual-only`, a temporary configuration, and a loopback port.

After a completed MSVC release build of OpenRGB:

```powershell
python tests/room-network-images/build_test.py --build build
python FrameRouting/tests/sdk_integration.py --openrgb build/release/OpenRGB.exe --reader <path-to-read_surface.exe> --native-client tests/room-network-images/.build/native_image_client.exe --qt-bin <Qt>/bin
```

The reader is the separate JSON FrameSurface probe used by the enclosing integration fixture. Add the OpenRGB release directory to `PATH` when running the native helper separately, so its existing runtime DLLs can be found. Build outputs and response files stay in `.build/` and are ignored.

`native_image_client.exe PORT [legacy]` waits for two synthetic controllers. Normally it checks capability discovery, both image interfaces, invalid input rejection, exact immutable preview sharing, local preview expiration, repeated latest-frame submissions, server ACKs, bounded stop, and invalidation of old controller handles. Its last frame is 800×600, sequence 777, blue channel 173, with a 4-second lease. The enclosing Python fixture reads both native shared-memory surfaces to prove delivery. `legacy` instead requires image capability to be absent and image submission to be refused.

The client extension requires both SDK version 7 or later and the Room image feature flag. It does not change legacy LED descriptions or packets. SDK 7 is a fork extension, not an upstream allocation. The worker retains at most 64 latest output frames and 64 MiB of retained pixel storage, coalesces per controller/zone, and respects advertised frame pacing. One immutable frame and its encoded packet may additionally be in flight (each at most 64 MiB). There is no unbounded frame FIFO. Preview is the submitted image, not a remote screen capture.

TCP writes handle partial progress and have a 2-second packet bound (also limited by the source lease). A partial packet that cannot finish closes the socket: dropping bytes in place would corrupt framing. Disconnect invalidates image generations and queued frames; stale controller objects cannot submit into a reconnected session. Stop closes the socket before joins, and listener creation/replacement is serialized with stop. The ordinary connection attempt retains its pre-existing configurable connection timeout.

`GetImageAck` is diagnostic: it reports the count and latest status for a controller in the current connection. The wire ACK has no frame sequence, so it is not an acknowledgement of an arbitrary individual queued frame. The integration test drains earlier activity before requesting the final ACK, then independently checks the resulting surface pixels.
