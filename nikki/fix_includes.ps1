$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$pairs = @(
  @('RENDERTEST_tostr.inl', 'renderdoc_tostr.inl'),
  @('RENDERTEST_serialise.inl', 'renderdoc_serialise.inl'),
  @('RENDERTEST_pipestate.inl', 'renderdoc_pipestate.inl'),
  @('RenderTestcmd.h', 'renderdoccmd.h'),
  @('rendertestcmd.h', 'renderdoccmd.h'),
  @('rendertest_app.h', 'renderdoc_app.h'),
  @('rendertest_replay.h', 'renderdoc_replay.h'),
  @('RENDERTEST_CaptureOptions', 'renderdoc_capture_options')
)
$count = 0
Get-ChildItem 'D:\git\rendertst-nikki\renderdoc','D:\git\rendertst-nikki\renderdoccmd' -Recurse -Include '*.cpp','*.h','*.c','*.rc','*.vcxproj','*.inl','*.filters' | ForEach-Object {
  $c = [System.IO.File]::ReadAllText($_.FullName)
  $n = $c
  foreach ($p in $pairs) { $n = $n.Replace($p[0], $p[1]) }
  if ($n -ne $c) { [System.IO.File]::WriteAllText($_.FullName, $n, $utf8NoBom); $count++ }
}
Write-Output "fixed: $count"
