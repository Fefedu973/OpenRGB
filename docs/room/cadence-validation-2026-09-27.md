# Room cadence and scene validation — 27 September 2026

The deployed core color mailbox and Visual Map batching remove the measured
shared routing delay. Two passive ten-second windows were recorded before and
after, outside profile switching and compilation. The existing Wallpaper Engine
bridge was kept unchanged.

| Output measurement | Before | After |
| --- | --- | --- |
| Wallpaper SDK color payloads per second | 6.48–6.58 | 50.06–53.49 |
| Nine Wi-Fi outputs, datagrams per second each | 7.37–7.60 | 50.85–54.80 |
| Wallpaper median interval | 125.84–127.70 ms | 17.27–17.41 ms |
| Wallpaper largest interval | 292.21–408.04 ms | 37.58–51.96 ms |

The wallpaper observations contain 497 and 532 bodies of exactly 2,054 bytes,
excluding retransmissions, rather than counting both header and body as frames.
LAN observations count datagrams. These are delivery measurements, not camera
measurements of physical LED refresh. The user separately confirmed that the
animations were noticeably smoother. Per-device limits remain: this does not
make a ten-Hz monitor or a slow driver update at fifty Hz.

The original instrumented router spent about 4,082 ms per ten-second window in
NVIDIA SetColor calls, 3,660 ms for one Corsair controller and 1,460 ms for one RAM
controller. Wallpaper SetColor calls consumed about 1.4 ms. The shared color
lock, held by blocking device I/O, was the cause addressed by the mailbox.

After the change, OpenRGB consumed 57–66% of one CPU core in the two windows,
the wallpaper bridge 3–4%, and Elgato 93–106%. These are not percentages of the
whole multicore PC. More frames entail more rendering work. GPU readings were
brief snapshots and do not establish continuous utilization or power use.

The exact binary hashes and source commits are in the local package's
`BUILD-INFO.json`. Production worker tests passed 82 assertions; the real Visual
Map DLL also passed interface discovery, combined-segment delivery and unload
tests. Six additional GLSL presets passed actual GPU compilation and rendering
at 800 x 500. Eleven Stream Deck shortcuts were invoked through their real
Windows `.lnk` files and each returned successful SDK profile acknowledgement
and the expected active name. Selection took approximately 1.5–4.7 seconds.

Runtime SDK snapshots found thirty controllers, no nonblack compatibility
buffers in Full Noir, twenty-eight in Full Blanc and twenty-eight changing
buffers with Room Pulse. Image outputs have separate native image paths, so a
black compatibility buffer alone is not proof of a black screen. Only the map
selected by the active profile was registered. The original Full Scale retained
125 members unchanged; the requested ASUS member moved to X274.97, Y105.87.
An application restart restored the last plugin session. This is not a Windows
reboot test. Temporary diagnostic profiles were archived outside the daily
profile directory; raw captures, addresses and user profiles remain private.
