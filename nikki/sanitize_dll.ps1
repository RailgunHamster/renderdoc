$src = 'D:\git\renderdoc-nikki\x64\Release\rendertest.dll'
$dst = 'D:\git\renderdoc-nikki\nikki\test\rendertest.dll'
$bytes = [System.IO.File]::ReadAllBytes($src)

# ASCII/UTF-8 variants: replace "renderdoc" (9) -> "rendertst" (9), "RenderDoc" -> "RenderTst", "RENDERDOC" -> "RENDERTST"
$pairs = @(
  @([System.Text.Encoding]::ASCII.GetBytes('renderdoc'), [System.Text.Encoding]::ASCII.GetBytes('rendertst')),
  @([System.Text.Encoding]::ASCII.GetBytes('RenderDoc'), [System.Text.Encoding]::ASCII.GetBytes('RenderTst')),
  @([System.Text.Encoding]::ASCII.GetBytes('RENDERDOC'), [System.Text.Encoding]::ASCII.GetBytes('RENDERTST'))
)
# UTF-16 variants
$pairs += @(
  @([System.Text.Encoding]::Unicode.GetBytes('renderdoc'), [System.Text.Encoding]::Unicode.GetBytes('rendertst')),
  @([System.Text.Encoding]::Unicode.GetBytes('RenderDoc'), [System.Text.Encoding]::Unicode.GetBytes('RenderTst')),
  @([System.Text.Encoding]::Unicode.GetBytes('RENDERDOC'), [System.Text.Encoding]::Unicode.GetBytes('RENDERTST'))
)

$count = 0
foreach ($p in $pairs) {
  $needle = $p[0]; $repl = $p[1]
  for ($i = 0; $i -le $bytes.Length - $needle.Length; $i++) {
    $match = $true
    for ($j = 0; $j -lt $needle.Length; $j++) {
      if ($bytes[$i + $j] -ne $needle[$j]) { $match = $false; break }
    }
    if ($match) {
      for ($j = 0; $j -lt $repl.Length; $j++) { $bytes[$i + $j] = $repl[$j] }
      $count++
      $i += $needle.Length - 1
    }
  }
}
Write-Output "replaced $count occurrences"
[System.IO.File]::WriteAllBytes($dst, $bytes)
Write-Output "written to $dst"
