$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$pairs = @(
  @('RENDERTEST_replay.h', 'renderdoc_replay.h'),
  @('RENDERTEST_app.h', 'renderdoc_app.h')
)
$count = 0
Get-ChildItem 'D:\git\rendertst-nikki\renderdoc','D:\git\rendertst-nikki\renderdoccmd','D:\git\rendertst-nikki\qrenderdoc' -Recurse -Include '*.cpp','*.h','*.c','*.vcxproj' | ForEach-Object {
  $c = [System.IO.File]::ReadAllText($_.FullName)
  $n = $c
  foreach ($p in $pairs) { $n = $n.Replace($p[0], $p[1]) }
  if ($n -ne $c) { [System.IO.File]::WriteAllText($_.FullName, $n, $utf8NoBom); $count++ }
}
Write-Output "fixed: $count"
