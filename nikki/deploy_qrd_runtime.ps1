# Deploy Qt runtime dependencies for qrendertest.exe (conda Qt needs its
# third-party DLLs alongside). Run after rebuilding the GUI.
param(
  [string]$ReleaseDir = "D:\git\rendertst-nikki\x64\Release",
  [string]$QtBin = "C:\tools\Anaconda3\Library\bin"
)

$ErrorActionPreference = "Stop"

$deps = @(
  "zlib.dll",
  "libpng16.dll",
  "icuin73.dll",
  "icuuc73.dll",
  "icudt73.dll",
  "zstd.dll",
  "libcrypto-3-x64.dll",
  "libssl-3-x64.dll",
  "Qt5Core_conda.dll",
  "Qt5Gui_conda.dll",
  "Qt5Widgets_conda.dll",
  "Qt5Svg_conda.dll",
  "Qt5Network_conda.dll"
)

# embedded python36 runtime (GUI Python shell)
$pyBase = "D:\git\rendertst-nikki\qrenderdoc\3rdparty\python"
foreach ($f in @("python36.dll", "python36.zip", "_ctypes.pyd")) {
  $src = Join-Path $pyBase $f
  if (Test-Path $src) { Copy-Item $src "$ReleaseDir\" -Force }
  else {
    $src64 = Join-Path "$pyBase\x64" $f
    if (Test-Path $src64) { Copy-Item $src64 "$ReleaseDir\" -Force }
  }
}

New-Item -ItemType Directory -Path "$ReleaseDir\platforms" -Force | Out-Null
Copy-Item "$QtBin\..\plugins\platforms\qwindows.dll" "$ReleaseDir\platforms\" -Force

foreach ($d in $deps) {
  $src = Join-Path $QtBin $d
  if (-not (Test-Path $src)) { Write-Warning "MISSING in Qt bin: $d"; continue }
  Copy-Item $src "$ReleaseDir\" -Force
}

Write-Output "Qt runtime deployed to $ReleaseDir"
