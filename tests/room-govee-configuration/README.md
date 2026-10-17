# Govee LAN configuration after a firmware update

This offline regression compiles the production `RGBController.cpp` (including
`CompareControllers` and configuration/serialization methods) and extracts the
unchanged bodies of `ProfileManager::LoadControllerFromListWithOptions` and
`SaveConfiguration`. Only the application resource manager and hardware boundary
are replaced by inert controllers. The generated configuration is written under
this test's ignored `.build` directory, never to an OpenRGB user configuration.

From an x64 MSVC developer prompt at the repository root:

```console
python tests/room-govee-configuration/run.py
python tests/room-govee-configuration/run.py --without-fix
```

The first command passes **96 assertions**. The second is an intentional negative
control: restoring the old strict version predicate makes the observed firmware
update fail at the first restoration assertion (exit 1).

The representative H61E1 changes only Wi-Fi software `1.01.17` to `1.06.24`.
The real restore path reinstates segments of 4, 11 and 15 LEDs at offsets 0, 4
and 15. The real save path writes exactly one controller using the new firmware
version and retains those segments. The fixture uses a synthetic serial/address.
Configured matrix geometry also survives a detected linear default. A hardware
change is rejected and preserves the offline saved entry instead of replacing it.

The exception is limited to two Govee controllers with the same device type,
hardware name, description and nonzero 12- or 16-digit hexadecimal serial. Both
version strings must contain exactly the four known driver fields with dotted
three-component numbers; hardware fields must match. Zone count, minimum and
maximum LED capacities must match before any configuration mutation. Existing
configurable zone name/type rules remain applicable, and segments are restoration
payload rather than part of the fresh-discovery identity. Existing location and
mode checks remain in their respective callers. All other vendors and exact
version matching retain their previous behavior.

Tests reject missing/different/invalid serials, other vendors, changed controller
identity/location, changed hardware, unknown/duplicate/missing version fields,
malformed values and incompatible zone topology before configuration callbacks.
They also cover 12-digit identities and BLE software-only updates.

These tests prove restoration and saving behavior without any device I/O. They
do not alter a layout or change Full Scale.

A separate installed-build check on 17 October 2026 verified the actual H61E1
after the firmware update: all three saved segments were restored, all 30 SDK
colors were white, and an eight-second passive capture contained 88 valid B0
frames with 30 white LEDs each. The user confirmed the strip was lit. No test
color/power commands were sent and the Full Scale file remained byte-identical.
