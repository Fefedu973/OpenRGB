# Channel rebuild regression

`run.py` extracts the actual `SetupZones`, `DeviceUpdateZoneLEDs` and
`DeviceUpdateSingleLED` methods from the two production controllers at build
time. USB output and the RGBController storage boundary are replaced with small
fakes; no HID code, application or configuration is opened.

From an MSVC developer prompt, run `python run.py --out <temporary-directory>`.
Changing `[2,3]` LEDs into `[4,1]` must route LED 3 to the first channel. Repeated
growth, shrink, empty channels and different physical Nollie channel numbers are
covered. `--without-fix` removes only the two clears and reproduces the incorrect
channel on the second configuration. It is intentionally expected to fail.

The same runner includes the production Govee model table, `SetupZones` and
`DeviceConfigureZone`: the four room models keep their fixed counts and complete
frame capability while allowing logical segments/matrices; invalid zone indexes
produce no output call. This does not test UDP delivery or device firmware.

The production ASUS Aura `SetupZones`, `DeviceConfigureZone` and update methods
are also extracted. The fixed four-LED zone exposes type/segments without resize;
three onboard LEDs and the trailing 12 V LED remain distinct segments at offsets
0 and 3 across repeated configuration. ARGB counts and routing after the fixed
zone stay unchanged. The run currently passes 218 assertions without hardware.
