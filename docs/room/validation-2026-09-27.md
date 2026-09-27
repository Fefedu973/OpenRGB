# Room integration validation — 27 September 2026

The initial table records software tests. The subsequent native hardware pass
is recorded separately below; source coverage is not a completed room migration.

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

Remaining validation: full-runtime integration beyond the selected native tests
below, real capture/GPU throughput, device identity/LED bindings for the imported room canvas, and the
full user migration. The existing Elgato compositor remains required. A native
wallpaper renderer, GPU texture sharing, HDR handling and the deferred Screen
Ambience layout are not included in this delivery.

## Subsequent native hardware pass

All tests below ran after stopping SignalRGB, its local bridge supervisor and
the separate OpenRGB Windows service. Closing the OpenRGB tray application alone
had left the old service alive in session 0 on SDK port 6742.

* **AW3426DW 187C:101D**: the actual C++ HID driver, unauthenticated Realtek
  protocol, independent zone masks 1 and 8, 100 ms minimum transfer interval.
  The user confirmed both logo and power button during the paced rainbow.
  No claim that the protocol can read an original color is made.
* **Alienware software equivalence**: all 23 installed SignalRGB profiles,
  2,496 color packets and 320 responses across five OEM keys match compiled C++.
  The scheduling regression tests latest-frame replacement, grouped masks,
  per-zone fairness, deduplication and interruptible idle shutdown. An in-progress
  synchronous HID call still has to finish before shutdown joins the worker.
* **Stream Deck MK.2**: embedded Frida Core, no Python/HTTP; discovery occurred
  on a natural Elgato composition. 126 accepted/acknowledged images, 1,890 tile
  paints across fifteen keys, about 16 fps at a 20 fps cap, ACK p95 about 7 ms,
  no reported compositor error, background restoration confirmed. The user
  confirmed the background and retained icons. First discovery still depends on
  a natural Elgato composition; it can wait on a static page.
* **KBHE 75HE firmware 2.0.10**: native RAW HID, 90 frames / 82 LEDs, restore
  succeeded after fixing the orphan-live-mode case. The existing live pixels
  are read before mutation and restored if firmware command 0x76 has no saved
  hardware effect; the result explicitly says the earlier hardware effect is
  unknown. No firmware change was made.
* **G502 X PLUS**: read-only native C547 probe confirmed the name, 8071/8081
  indices/versions and eight zones. The complete executable now detects it too,
  after fixing false receiver slots created from timeouts and long-report reads.
  This is not an optical color test or physical
  G915/coexistence validation; see the linked Logitech evidence.

* **Govee H6008 ×3 and H6159**: the user confirmed all four respond and the
  H6008 transitions are immediate. The native integration powers them on itself.
  Each H6008 completed 63 submitted frames with exact power, brightness and mode
  readback after restoration. A 60-frame rainbow took about 4.9 seconds.
  The H6159 also passed black/off/automatic resume and exact restoration; after
  reducing unnecessary readbacks, its rainbow took 12.154 seconds (previously
  16.417 seconds). These are observed diagnostic rates, not promised output FPS.
  Restoration is verified on the existing authenticated connection before close.
  Follow-up identified the same-process reconnect failure: firmware retains the
  authenticated session while Windows closes/reopens the GATT objects. The native
  transport now verifies the previous session with exact AA14 identity before
  reuse, with fresh authentication if verification fails. Nine connections across
  all three bulbs, plus three complete transport destructions/recreations, passed
  with 36 successful state queries and no color/power commands. A bounded in-memory
  cache survives rescans (16 entries, five-minute expiry, exact credentials scope,
  mandatory AA14). Physical radio loss/power cycling remains untested in this run.
* **Web Page effect**: the actual WebView2 runtime rendered local HTML and an
  animated HTTP loopback page at 800×600. 61 assertions covered pixels, routing,
  changing URLs, restart and cancellation. No RGB hardware was opened in this
  effect test. The full Effects DLL builds and the loader/license are packaged.
  See the Effects repository's `Documentation/WEBPAGE.md` for performance limits.
* **Complete unprivileged inventory**: 23 controllers exported in the actual
  ProfileManager JSON, including eight LAN Govee devices and the mouse. Targeted
  discovery supplements multicast on the same UDP socket; stable MAC matching
  still rejects a different device at an old IP. Detection took about 5.2 seconds.
  Four DRAM devices remain absent from this unprivileged run because PawnIO
  access requires administrator/service permissions. Wallpaper's existing SDK
  server was disabled and therefore not enumerated.
* **Save-only CLI**: no longer applies unsolicited modes/colors before saving.
  Previously a Govee static mode required one unspecified color and called
  `exit(0)` before saving. Fifteen extracted-production assertions pass; removing
  the guard reproduces zero exit with a missing completion marker. The corrected
  full run confirms the saved profile and normal device cleanup.
* **Imported component configuration**: the actual application loaded the
  prepared private configuration and saved a second inventory. All 26 configured
  zones, 91 segments and 932 LED routes match the prepared definitions; the only
  serialization normalization is omission of an empty matrix-map array. Native
  bounds and segment offsets are therefore verified beyond the synthetic tests.
  This does not activate the Visual Map layout or validate its appearance.

## Final Full Scale migration

The subsequent administrator ENE scan detected four DIMMs, each with eight LEDs,
at addresses 0x70 through 0x73. A fresh ASUS-only executable export confirmed the
fixed four-LED zone's type/segment capabilities, without exposing a resize flag.
The final import links all 101 active source elements to 111 members: 1,120 LED
routes, one native Stream Deck image and one 512-sample wallpaper SDK4 matrix.
The original SignalRGB layout and registry are unchanged.

37 Python import tests and 218 native reconfiguration assertions pass. Both
installed maps were parsed by the actual Qt codec. Independent C++ routing tests
checked 9,056 assertions over Stream Deck and wallpaper image geometry, including
all compatibility/sample cells. The maximum UV error was 4.33e-15. The 103 members
present in the earlier export retain their exact geometry. Full Scale is prepared
for automatic loading/registration; the Music layout is kept manual to prevent
two layouts from competing for the same physical outputs.

Wallpaper Engine is the explicitly requested exception to bridge retirement.
Its running application now serves one controller at 127.0.0.1:6743, negotiating
SDK4 from an SDK7 request. Only its source and SDK-server settings changed; all
20 wallpaper assets retain their hashes. Govee, NVIDIA and Stream Deck bridge
processes and their supervisor startup were stopped/removed.

## Installed startup and full-scene runtime

The `OpenRGB Room` task starts the native executable directly, elevated in the
interactive user session, 15 seconds after logon. Old SignalRGB/OpenRGB startup
entries are disabled, and their services are stopped/manual. No Govee, NVIDIA or
Stream Deck bridge process remains. The Wallpaper Engine exception is retained.
Startup definitions were backed up before mutation.

The 21 unused Corsair/Nollie outputs are explicitly configured with zero LEDs.
The zone-initialization check remains enabled. A restart no longer requires the
dialog to be dismissed, and the real SDK snapshot has no unconfigured zones.

The first complete GUI run exposed a missed Effects remap when Visual Map
registered its virtual controller on the GUI thread. Same-thread blocking Qt
delivery is invalid: Qt 6.8.3 rejected the test call without remapping. Effects
now dispatches directly on its own thread and synchronously queues other threads.
Actual Qt regression tests cover both cases. After the fix, Full Scale and its
800x500, 30 FPS spatial shader start without manual reconfiguration. Two live SDK
snapshots, one second apart, show animated color buffers on 25 controllers,
including the 512-point wallpaper. Stream Deck and the virtual canvas use native
images, so their black compatibility buffers are not evidence of a black image.

During this final run the AW3426DW USB interface and G502 receiver were absent
from Windows; their saved mappings are retained. Earlier native monitor output
was visually confirmed, and earlier mouse discovery/zone mapping was verified,
but this run does not establish mouse light output or G915 coexistence.

The four BLE devices enter streaming automatically. Two H6008 connections still
drop periodically under the observed radio conditions; native session recovery
reconnects them. A bounded AA01 retry does not prevent a real link loss. The
implementation and diagnostics distinguish missed replies from closed sessions;
this observation must not be reported as uninterrupted long-term BLE operation.

The follow-up BLE change aborts a reply wait when an already-authenticated GATT
session closes, without retrying that closed link. Initial authentication is
exempt. Fifty session/authentication/query tests, crypto tests and compilation
of the three Windows translation units pass.

Intermittent native Stream Deck response warnings led to a confirmed race:
the RPC callback could publish its ready flag before copying the JSON payload.
The response, errors and request ID now share a mutex; detachment is atomic.
The old callback reproduces a ready/null result in the regression test. The fix
passes 2,000 concurrent 16 KB responses plus a real Frida synthetic-child test
with twenty 311,040-byte frames, bounded timeout, restoration and close. This
establishes the race fix, not the cause of every prior live warning.

The final installed package was started again through the Windows task. Its
SDK7 server answered a same-profile reload in 1.703 seconds and remained usable.
A subsequent pair of snapshots again showed 25 changing LED buffers, both image
output descriptors and zero unconfigured zones. The initial four BLE devices
reached streaming within 14 seconds. Observed H6008 link closure is now reported
as a closed session rather than waiting for both reply timeouts. These checks
are runtime/software evidence; the latest full-scene optical check has not been
confirmed by the user. Generated inventory profiles are archived outside the
daily profile directory.
