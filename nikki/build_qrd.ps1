# One-shot build + package of the RenderTest GUI (qrendertest.exe).
# Usage:  powershell -File nikki\build_qrd.ps1
# Steps:  moc -> qmake -> nmake (release) -> deploy Qt runtime -> package zip + SFX exe
param(
  [string]$RepoRoot = "D:\git\rendertst-nikki",
  [string]$Vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
)

$ErrorActionPreference = "Stop"
$qrd = Join-Path $RepoRoot "qrenderdoc"
$rel = Join-Path $RepoRoot "x64\Release"
$qtBin = "C:\tools\Anaconda3\Library\bin"
$moc = "$qtBin\moc.exe"

$vcvarsQ = "`"$Vcvars`""

# 1. regenerate the manual moc for ScintillaQt.h (qmake's auto-moc misses it)
New-Item -ItemType Directory -Path "$qrd\release" -Force | Out-Null
& cmd /c "call $vcvarsQ >nul 2>&1 && cd /d `"$qrd`" && `"$moc`" -DUNICODE -DWIN32 -DWIN64 -D_WIN32 -D_WIN64 -DRENDERTEST_PLATFORM_WIN32 -DSCINTILLA_QT=1 -DSCI_LEXER=1 -I. -I..\renderdoc\api\replay -I3rdparty\scintilla\include\qt -I3rdparty\scintilla\include -I3rdparty\scintilla\src -I3rdparty\scintilla\lexlib -I`"$qtBin\..\include\qt`" -I`"$qtBin\..\include\qt\QtCore`" -I`"$qtBin\..\include\qt\QtGui`" -I`"$qtBin\..\include\qt\QtWidgets`" -I`"$qtBin\..\include\qt\QtNetwork`" -o release\moc_ScintillaQt.cpp 3rdparty\scintilla\qt\ScintillaEditBase\ScintillaQt.h"
if (-not (Test-Path "$qrd\release\moc_ScintillaQt.cpp")) { throw "moc_ScintillaQt.cpp not generated" }
Write-Output "[1/5] moc_ScintillaQt.cpp OK"

# 2. qmake
& cmd /c "call $vcvarsQ >nul 2>&1 && cd /d `"$qrd`" && qmake -spec win32-msvc qrenderdoc.pro" 2>&1 | Select-String -Pattern "Error|error" | ForEach-Object { Write-Warning $_ }
Write-Output "[2/5] qmake OK"

# 3. nmake release
& cmd /c "call $vcvarsQ >nul 2>&1 && cd /d `"$qrd`" && nmake /NOLOGO /f Makefile.Release" 2>&1 | Select-Object -Last 3 | Write-Output
if (-not (Test-Path "$rel\qrendertest.exe")) { throw "build failed: qrendertest.exe missing" }
Write-Output "[3/5] nmake OK -> $rel\qrendertest.exe"

# 4. deploy Qt/python runtime deps next to the exe
& "$PSScriptRoot\deploy_qrd_runtime.ps1" -ReleaseDir $rel
Write-Output "[4/5] runtime deps OK"

# 5. package zip + single-file SFX
& "$PSScriptRoot\package_qrd.ps1" -ReleaseDir $rel
Write-Output "[5/5] package OK"
