# Curved custom ribbons

Visual Map already supports fractional per-LED coordinates. A bent cable does
not need a rectangular device matrix or a device-specific renderer.

`curve_custom_ribbon.py` prepares a new map file from a group of CUSTOM members
sharing the same transform. It bends their local longitudinal axis into a
parabolic arch, preserving every LED ID, strand, segment identity, endpoint and
existing scale/rotation. The separate members remain separate hardware routes.

```text
python tools/room-layouts/curve_custom_ribbon.py map.json map-curved.json --member-name "Cable group" --direction up
```

`--strength` is the midpoint displacement as a fraction of the endpoint chord
(default 0.25). `up`/`down` are canvas directions after the existing affine
transform. A ribbon whose transverse axis is horizontal cannot use these
vertical directions. The command refuses an existing output file and never
edits the source or connects to OpenRGB.

The layout bounds and translation are recomputed together. Editing only the
custom shape height would move a rotated cable because the affine transform
rotates around that shape's center. The command checks that all original
endpoint positions remain equal within 1e-8 canvas units. Nonfinite transforms,
nonpositive scales/dimensions and invalid origins are rejected before mutation.

For live installation, first save a complete plugin-state snapshot so pending
Visual Map edits are flushed. Back up and validate the map files, replace them
atomically, then reload every affected open map and restore the complete effect
snapshot. Loading a VM-only profile clears Effects; it does not preserve a
running animation. A restore preserves controls but restarts animation time.
Avoid concurrent UI geometry edits during that short transaction. Historical
diagnostics/backups are not active layouts and should remain intact.
