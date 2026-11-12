# Rubia Engine icon

The approved mark is one madder-red dye brushstroke on a transparent background.
`rubia-dye-source.png` is the original artwork. The `rubia-icon-*.png` files and
`rubia.ico` preserve its shape, colors and alpha, resampled at 16, 24, 32, 48, 64,
128 and 256 pixels. Regenerate them with `./assets/branding/BuildIcons.ps1` on Windows.

The 64-pixel PNG is embedded by CMake for the native window icon. The Windows
executable also embeds the multi-resolution ICO. Neither depends on the working
directory or an imported scene asset. The native window title and Windows version
metadata use the root CMake project version. The editor's internal menu bar is
unchanged.

The original artwork was generated with the built-in imagegen tool, then approved
by the user. Prompt: a single confident diagonal stroke of madder-red dye, a broad
connected silhouette, saturated beginning and tapered bristle ending, restrained
retro graphic character, transparent background, no lettering, container, badge,
3D bevel, shadow or splatters.

`concepts/` contains earlier exploratory renders and Blender scenes; these are
not used as the application icon.
