$procs = Get-Process -Name "xstarter" -ErrorAction SilentlyContinue
foreach ($proc in $procs) {
    try {
        $id = [Security.Principal.WindowsIdentity]::new($proc.Handle)
        $principal = New-Object Security.Principal.WindowsPrincipal($id)
        $isElevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
        Write-Host ("PID={0} Elevated={1}" -f $proc.Id, $isElevated)
    } catch {
        Write-Host ("PID={0} Error={1}" -f $proc.Id, $_.Exception.Message)
    }
}
