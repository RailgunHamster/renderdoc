$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$pairs = @(
  @('RENDERTEST_replay.h', 'renderdoc_replay.h'),
  @('RENDERTEST_app.h', 'renderdoc_app.h'),
  @('$(SolutionDir)rendertest\', '$(SolutionDir)renderdoc\'),
  @('..\..\rendertest\', '..\..\renderdoc\'),
  @('..\rendertest\', '..\renderdoc\'),
  @('\rendertest\', '\renderdoc\'),
  @('$(ProjectDir)rendertest\', '$(ProjectDir)renderdoc\')
)
$count = 0
Get-ChildItem 'D:\git\renderdoc-nikki' -Recurse -Include '*.vcxproj','*.filters','*.sln' | Where-Object { $_.FullName -notmatch '\\3rdparty\\' } | ForEach-Object {
  $c = [System.IO.File]::ReadAllText($_.FullName)
  $n = $c
  foreach ($p in $pairs) { $n = $n.Replace($p[0], $p[1]) }
  if ($n -ne $c) { [System.IO.File]::WriteAllText($_.FullName, $n, $utf8NoBom); $count++; Write-Output "fixed: $($_.Name)" }
}
Write-Output "total fixed: $count"
