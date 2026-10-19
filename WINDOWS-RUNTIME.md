# Windows runtime packaging (v0.0.3)

This patch packages the Microsoft C++ runtime DLLs required by the Windows x64
executable: `MSVCP140.dll`, `VCRUNTIME140.dll`, and `VCRUNTIME140_1.dll`.
The previous ZIP omitted them and required an existing VC redistributable.

The game executable and all other existing payload files are byte-for-byte
identical to v0.0.2; the patch adds the redistributable DLLs and this
packaging note. Existing renderers, settings, audio defaults, mods and saves
retain their previous behavior. No ROM, BIOS, player settings or saves ship.

The source pins the shared NESRecomp correction in
https://github.com/mstan/nesrecomp/pull/74 so future cycle builds stage the
compiler's redistributables beside the executable. Use the game's release
recipe, or `nesrecomp/tools/package_cycle_windows.ps1` for the generic package.
Runtime DLLs must come from the licensed Visual Studio redistributable directory.

Release reproduction: take `SuperMarioBros2JapanFDSRecomp-Japan-FDS-v0.0.2-windows-x64.zip` (SHA256
`ccec1d3187c1407223d7e0909203cd0443d648e69b5032a3f959dc43622ac1f7`), preserve every original entry, and add this note as
`WINDOWS-RUNTIME.md` plus the three x64 runtime DLLs. Check the complete PE import
closure, launch the clean extraction, and compare hashes/screenshots to the
previous package. The retained release evidence records each added DLL's hash.

Tracking: `beads-2dw.1.97`.
