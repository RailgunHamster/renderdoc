$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$count = 0
Get-ChildItem 'D:\git\rendertst-nikki' -Recurse -Include '*.vcxproj','*.filters' | Where-Object { $_.FullName -notmatch '\\3rdparty\\' } | ForEach-Object {
  $c = [System.IO.File]::ReadAllText($_.FullName)
  $n = $c
  # directory paths back to renderdoc\
  $n = $n.Replace('$(SolutionDir)RenderTest\', '$(SolutionDir)renderdoc\')
  $n = $n.Replace('$(SolutionDir)rendertest\', '$(SolutionDir)renderdoc\')
  $n = $n.Replace('..\..\RenderTest\', '..\..\renderdoc\')
  $n = $n.Replace('..\RenderTest\', '..\renderdoc\')
  # project file references back to renderdoc_*.vcxproj
  $n = [regex]::Replace($n, 'RENDERTEST_([a-z0-9_]+)\.vcxproj', 'renderdoc_$1.vcxproj')
  $n = [regex]::Replace($n, 'rendertest_([a-z0-9_]+)\.vcxproj', 'renderdoc_$1.vcxproj')
  # source file references that point at renderdoc_*.cpp files
  $n = [regex]::Replace($n, '(ClCompile|ClInclude|ResourceCompile Include=")[^"]*?rendertest_([a-z0-9_]+)\.(cpp|h|rc|inl)', '$1$2.$3')
  $n = $n.Replace('RenderTestcmd.rc', 'renderdoccmd.rc')
  $n = $n.Replace('RenderTest.rc', 'renderdoc.rc')
  if ($n -ne $c) { [System.IO.File]::WriteAllText($_.FullName, $n, $utf8NoBom); $count++ }
}
Write-Output "fixed: $count"
