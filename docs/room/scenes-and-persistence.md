# Room scenes and persistent settings

The room uses named **profiles** to select an Effects preset and its Visual Map
together. Device zones, segments and physical controller identities are not
changed when selecting these profiles.

| Profile | Map | Output |
| --- | --- | --- |
| Full Scale - Rainbow | Full Scale | Existing continuous spatial rainbow |
| Full Blanc | Full Scale | Constant white on LEDs and image backgrounds |
| Full Noir | Full Scale | Black on LEDs and image backgrounds |
| Music - Tri Band | Music - Tri Band | Three audio bands, pulses, rising meters and surface visualization |
| Ambilight - Web Page | Full Scale | The page served at `https://localhost:8443` |

Stream Deck button icons remain managed by Elgato. Full Noir blacks out the
lighting/background; it does not remove icons or turn off the desktop displays.
The white and black presets maintain their image leases at 10 FPS. Ambilight
requests 20 FPS at 800 × 500; actual rendering throughput depends on WebView2,
the source page and the GPU. Both canvases retain the room's 320:200 aspect ratio.

## Music

The shader uses the Effects plugin's native system-output loopback. The stable
“System default output (Loopback)” selection follows a change of default output
device and retries a temporarily unavailable endpoint. It does not use a
microphone or a separate audio bridge. Audio settings remain editable in the
shader's settings: input, amplitude, smoothing, decay and equalizer.

The dedicated 320 × 200 map preserves every routed device/segment and LED index.
Its three columns are bass, mid and treble. Small lamps and fans pulse in the
upper bass area. The keyboard, mat, Stream Deck and wallpaper span the three
columns. Below them, strips and cables form meters rising from their first LED;
native segment offsets preserve continuity across split strips.

This is an original multi-band preset, not a reproduction of SignalRGB code.
The inherited DSP supplies 64 magnitudes expanded to 256 shader slots; those
slots should not be described as 256 independent FFT frequency bins. See the
Effects fork's `Documentation/Room-Music.md` for the exact shader contract.

## Ambilight

Start Better SignalRGB Screen Capture so its local HTTPS page is available,
then select **Ambilight - Web Page**. The browser renderer captures that page
directly. Full Scale remains the physical room map; arrange the screen regions
inside the capture application to match it. The website must use a certificate
trusted by Windows; this setup does not bypass TLS verification.

## Persistence

Visual Map edits now update the in-memory recovery state immediately, save the
canonical map atomically after a short debounce and flush before a rescan or
profile change. Temporarily disconnected members remain in the saved map.
Profiles select a canonical map file by name; they do not embed old positions
that could overwrite newer edits. Only one map emits at a time.

With **Remember last session** enabled, the GUI checkpoints plugin state only
when it changes, every two seconds and on graceful close. The checkpoint is
separate from named profiles: experimenting with an effect does not overwrite
the saved preset. Startup restores a valid checkpoint and otherwise falls back
to the configured startup profile. Native hardware mode/color snapshots are
not replayed by this feature. There is a small unsaved window if Windows or the
process terminates before the next checkpoint.

The General Settings page is rebuilt on the GUI thread, including after
detectors register additional settings. Refreshing the profile list no longer
emits selection changes that accidentally rewrite saved startup choices.

## Recreating the local presets

`tools/room-setup/create-room-scenes.py` is an offline generator. Supply the
current Full Scale map, a complete native SDK profile export (including RAM and
all strip segments), the existing rainbow profile and the music template from
the Effects fork. It writes a **new** output directory and never edits live
configuration or the source layout. Machine-specific identifiers stay private.

The two forks' persistence and audio tests exercise real Qt/plugin code with
synthetic devices. Hardware rendering, profile switching and a process restart
must also be checked after installing updated binaries.
