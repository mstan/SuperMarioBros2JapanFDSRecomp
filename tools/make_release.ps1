<#
make_release.ps1 -- build and zip the Windows release.

  release\SuperMarioBros2JapanFDSRecomp-windows-x64.zip
    SuperMarioBros2JapanFDSRecomp.exe, SDL2.dll, assets\, README.txt, LICENSE.txt

The zip never contains a disk image, the BIOS, a capture file or saves. Building
needs your own disk image and bios\disksys.rom (see README.md): the BIOS is
recompiled into the program. The player supplies both files again at run time.

Static coverage: code the game copies into RAM at run time is compiled from a
local capture file (cyc_captures.txt, gitignored: it holds game code bytes).
-Ingest regenerates it from your own image first: a headless build runs every
route in routes\routes.toml with --capture-log, then the release build compiles
what they ran. Without a capture file that code runs on the interpreter.

Publish after smoke-testing the zip from a scratch directory:
  gh release create vX.Y.Z release\SuperMarioBros2JapanFDSRecomp-windows-x64.zip --title "vX.Y.Z" --notes-file <notes.md>

Usage: powershell -File tools\make_release.ps1 [-Ingest] [-Captures FILE] [-SkipBuild] [-BuildDir build_release]
#>
param(
  [string]$BuildDir = 'build_release',
  [switch]$SkipBuild,
  [string]$CMake = '',
  [string]$Captures = '',
  [switch]$Ingest
)
$ErrorActionPreference = 'Stop'
$name = 'SuperMarioBros2JapanFDSRecomp'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$out = Join-Path $root 'release'
# Visual Studio's CMake: another cmake on PATH (e.g. MSYS2's) has no VS generator.
if (-not $CMake) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path $vswhere) {
    $vsroot = & $vswhere -latest -products * -property installationPath
    $candidate = Join-Path $vsroot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path $candidate) { $CMake = $candidate }
  }
  if (-not $CMake) { $CMake = 'cmake' }
}

if (-not $Captures) { $Captures = Join-Path $root 'cyc_captures.txt' }
$Captures = [IO.Path]::GetFullPath($Captures)
if ($Ingest) {
  # Capture pass: a headless build (with whatever captures exist) runs every route.
  $ingestBuild = Join-Path $root 'build_ingest'
  $pre = if (Test-Path $Captures) { "-DNESRECOMP_CYCLE_CAPTURES=$Captures" } else { '-DNESRECOMP_CYCLE_CAPTURES=' }
  & $CMake -S $root -B $ingestBuild -G 'Visual Studio 17 2022' -A x64 -DNESRECOMP_HEADLESS=ON $pre
  if ($LASTEXITCODE -ne 0) { throw "ingest configure failed ($LASTEXITCODE)" }
  & $CMake --build $ingestBuild --config Release --parallel
  if ($LASTEXITCODE -ne 0) { throw "ingest build failed ($LASTEXITCODE)" }
  $headless = Get-ChildItem -LiteralPath $ingestBuild -Recurse -Filter "$name.exe" | Where-Object { $_.FullName -notmatch '\\CMakeFiles\\' } | Select-Object -First 1
  python (Join-Path $root 'routes\run_routes.py') $headless.FullName --capture-log $Captures
  if ($LASTEXITCODE -ne 0) { throw "routes failed ($LASTEXITCODE)" }
}
$capArg = '-DNESRECOMP_CYCLE_CAPTURES='
if (Test-Path $Captures) { $capArg = "-DNESRECOMP_CYCLE_CAPTURES=$Captures"; Write-Host "static coverage: compiling captures from $Captures" }
else { Write-Warning "no capture file ($Captures): RAM code the game copies at run time will be interpreted; use -Ingest" }

if (-not $SkipBuild) {
  & $CMake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 -DNESRECOMP_DEV_UI=OFF -DNESRECOMP_HEADLESS=OFF $capArg
  if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }
  & $CMake --build $build --config Release --parallel
  if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
}
$cache = Join-Path $build 'CMakeCache.txt'
if (-not (Select-String -LiteralPath $cache -Pattern '^NESRECOMP_DEV_UI:BOOL=OFF$' -Quiet)) {
  throw 'refusing to package a build with NESRECOMP_DEV_UI on'
}
$exe = Get-ChildItem -LiteralPath $build -Recurse -Filter "$name.exe" | Where-Object { $_.FullName -notmatch '\\CMakeFiles\\' } |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "no $name.exe under $build" }
$bin = $exe.DirectoryName
foreach ($need in @('SDL2.dll', 'assets')) {
  if (-not (Test-Path (Join-Path $bin $need))) { throw "missing $need beside $($exe.FullName)" }
}

$stage = Join-Path $out $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe.FullName, (Join-Path $bin 'SDL2.dll') $stage
Copy-Item -Recurse (Join-Path $bin 'assets') $stage
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
@'
Super Mario Bros. 2 (Japan) - FDS Static Recompilation
======================================================

A native PC build of Super Mario Bros. 2 for the Famicom Disk System, produced by
statically recompiling its 6502 code with nesrecomp (github.com/mstan/nesrecomp).
The real FDS BIOS runs: you see the Nintendo boot screen and the disk load.

No game data is included. You need:
  - the disk image "Super Mario Bros. 2 (Japan) (Debug Value 0).fds"
    (SHA-1 3b8c8998b4887d6dd676965943d69a320738ab9c)
  - the FDS BIOS (any file name; 8192 bytes, CRC32 5E607DCF)

1. Run SuperMarioBros2JapanFDSRecomp.exe and select the disk image.
2. Settings > System > Select BIOS... and pick the BIOS (remembered in config.ini).
3. Play.

Keys: arrows, Z = A, X = B, Enter = Start, Backslash = Select,
D = Disk, Escape = Menu, Tab = fast-forward. Controller: A/X = A/B, LB = Disk, RB = Menu.
Disk: press once to see the drive state; press again while it shows to swap sides.
Rebind everything on the launcher's Controls page.

License: PolyForm Noncommercial 1.0.0 (LICENSE.txt). Super Mario Bros. 2 and the
Famicom Disk System are trademarks of Nintendo; not affiliated with Nintendo.
'@ | Set-Content -Encoding utf8 (Join-Path $stage 'README.txt')

$banned = Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {
  $_.Extension -in '.fds', '.qd', '.rom', '.nes', '.ips', '.fdssave', '.sav' -or $_.Name -match 'capture|config\.ini|rom\.cfg'
}
if ($banned) { throw "refusing to package: $($banned.FullName -join ', ')" }

$zip = Join-Path $out "$name-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Host "wrote $zip"
