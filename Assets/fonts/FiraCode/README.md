# Fira Code

- Upstream: https://github.com/tonsky/FiraCode
- Version: 6.2
- Source: https://github.com/tonsky/FiraCode/releases/download/6.2/Fira_Code_v6.2.zip
- File: unmodified `ttf/FiraCode-Regular.ttf` from the release archive.
- SHA-256: `5992ab9640e2df491b2f609467b1de60e8bc39b2c28db184342a0592d98f6117`
- License: SIL Open Font License 1.1; see `LICENSE` in this directory.

The editor loads Regular at 16 pixels before applying the window's DPI scale.
No Chinese font is merged. Dear ImGui's current text rendering uses individual
glyphs, so Fira Code's programming ligatures are not enabled by loading this font.

CMake copies this directory with the other runtime assets. Font lookup follows
the existing content convention: first relative to the working directory, then
relative to the source tree for development. If loading fails, the editor logs
a warning and uses ImGui's default font so the UI remains usable.
