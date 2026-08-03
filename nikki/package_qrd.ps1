# Package qrendertest GUI into a distribution zip + single-file SFX exe.
# Output: D:\git\rendertst-nikki\dist\qrendertest-win64.zip and ...qrendertest-win64.exe
param(
  [string]$ReleaseDir = "D:\git\rendertst-nikki\x64\Release",
  [string]$OutDir = "D:\git\rendertst-nikki\dist",
  [string]$SevenZip = "C:\Program Files\7-Zip\7z.exe"
)

$ErrorActionPreference = "Stop"

# ensure runtime deps are present first
& "$PSScriptRoot\deploy_qrd_runtime.ps1" -ReleaseDir $ReleaseDir

$files = @(
  "qrendertest.exe",
  "rendertest.dll",
  "python36.dll",
  "python36.zip",
  "_ctypes.pyd",
  "d3dcompiler_47.dll",
  "dbghelp.dll",
  "symsrv.dll",
  "symsrv.yes",
  "Qt5Core_conda.dll",
  "Qt5Gui_conda.dll",
  "Qt5Widgets_conda.dll",
  "Qt5Svg_conda.dll",
  "Qt5Network_conda.dll",
  "zlib.dll",
  "libpng16.dll",
  "icuin73.dll",
  "icuuc73.dll",
  "icudt73.dll",
  "zstd.dll",
  "libcrypto-3-x64.dll",
  "libssl-3-x64.dll"
)

$staging = Join-Path $env:TEMP "qrd_pkg"
Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path "$staging\platforms" -Force | Out-Null

foreach ($f in $files) {
  $src = Join-Path $ReleaseDir $f
  if (-not (Test-Path $src)) { Write-Warning "MISSING: $f"; continue }
  Copy-Item $src $staging -Force
}
Copy-Item "$ReleaseDir\platforms\qwindows.dll" "$staging\platforms\" -Force

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

# plain zip
& $SevenZip a -tzip "$OutDir\qrendertest-win64.zip" "$staging\*" -mx5 | Out-Null

# single-file SFX: extract to temp, run the GUI
$sfxConf = "$staging\sfx.conf"
Set-Content -LiteralPath $sfxConf -Value @"
;!@Install@!UTF-8!
InstallPath="%%T\qrendertest"
RunProgram="qrendertest.exe"
;!@InstallEnd@!
"@ -Encoding Ascii
& $SevenZip a -t7z "$staging\qrd.7z" "$staging\*" "-x!qrd.7z" "-x!sfx.conf" -mx5 | Out-Null

$sfxMod = Join-Path $env:TEMP "7zSfx-mod.sfx"
Copy-Item "C:\Program Files\7-Zip\7z.sfx" $sfxMod -Force -ErrorAction Stop
$bytes = [System.IO.File]::ReadAllBytes($sfxMod)
$confBytes = [System.Text.Encoding]::ASCII.GetBytes((Get-Content $sfxConf -Raw))
$out = $bytes + $confBytes
$seven = [System.IO.File]::ReadAllBytes("$staging\qrd.7z")
$out = $out + $seven
[System.IO.File]::WriteAllBytes("$OutDir\qrendertest-win64.exe", $out)

Get-ChildItem $OutDir | Select-Object Name, Length, LastWriteTime
Write-Output "Done: dist\qrendertest-win64.zip (double-click-extract) / qrendertest-win64.exe (single file)"
