# Native favorite effects in Lights

`prepare_favorites_menu.py` adds a **Favoris** child page without replacing any
existing Lights action. The existing Blanc, Éteindre, Musique, Ambilight, Rainbow
and Autres effets buttons remain intact. Up to fourteen effect shortcuts plus the
native Back action fit on a 5 × 3 page. A full parent page is rejected instead of
overwriting a button.

The tool reads the `Favori - *.json` profiles created by the Effects fork's
`tools/create-favorite-profiles.py`. Every entry must use a `SignalFavorite.*`
native effect. Buttons invoke the existing one-shot `ProfileSelect.exe` SDK
client; they do not start another OpenRGB or a bridge process.

Prepare outside the live `.sdProfile` using:

```text
python prepare_favorites_menu.py --profile-dir <profile.sdProfile> --lights-page <UUID> --profiles-dir <prepared-native-profiles> --icons <local-icon-directory> --helper <ProfileSelect.exe> --shortcuts <shortcut-directory> --output <new-proposal-directory>
```

Then create shortcuts with `Create-Shortcuts.ps1 -Proposal <proposal.json>`.
Inspect the generated manifests and verify all required OpenRGB profiles before
installing. Close Elgato normally, back up the complete profile, and merge the new
folder action into the current Lights manifest while preserving all prior
actions and page metadata. Copy the new child page and required local icons,
then restart Elgato. The preparation tool never writes into the live profile.

The September 27 installation contains thirteen shortcuts: Aurora, Custom Spiral,
Galaxies, Gradient, Gradient Wave, Rainbow, Rainbow Rise, Rainbow Tunnel, Side to
Side, Solid Color, Space, Spiral Rainbow and Underwater. Each real Windows shortcut
was executed and its selected profile acknowledged by the local OpenRGB server.
Twelve correspond to explicitly saved SignalRGB favorites; Galaxies is the
additional requested galaxy-family port.

This menu change is independent of the native compositor's support for empty
keys. It creates no dummy actions in unused positions and does not alter icons
to simulate an animated background.
