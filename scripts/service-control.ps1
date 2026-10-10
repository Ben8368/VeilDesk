param(
    [ValidateSet('Plan', 'Install', 'Uninstall')] [string]$Action = 'Plan',
    [string]$Executable = '',
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$name = 'VeilDeskSecureService'
$directory = Join-Path $env:ProgramFiles 'VeilDesk/SecureService'
$installed = Join-Path $directory 'veil-secure-service.exe'
$imagePath = '"' + $installed + '" --service'
$sc = Join-Path $env:SystemRoot 'System32/sc.exe'
function Invoke-ServiceTool([string[]]$Arguments) {
    & $sc @Arguments
    if ($LASTEXITCODE -ne 0) { throw "SCM operation failed: $($Arguments[0]) ($LASTEXITCODE)" }
}
try {
    if (-not $Executable) { $Executable = Join-Path $PSScriptRoot '../service/target/release/veil-secure-service.exe' }
    $source = [IO.Path]::GetFullPath($Executable)
    $hash = if (Test-Path -LiteralPath $source -PathType Leaf) { (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash } else { 'Unavailable; build first' }
    [ordered]@{ Action = $Action; Service = $name; Source = $source; SHA256 = $hash; Destination = $installed; Account = 'NT AUTHORITY\LocalService'; Start = 'Manual'; StartAfterInstall = $false; IPC = 'Diagnostic: SYSTEM/Administrators/LocalService; authorization: authenticated local clients, Service verifies installed Broker path and binds PID/creation/session/SID/image'; Rollback = 'No automatic rollback; retain files and inspect SCM on failure; verified Uninstall removes owned SCM entry'; Uninstall = 'Verify image/account, graceful stop, delete SCM entry, preserve all files'; Apply = [bool]$Apply } | ConvertTo-Json
    if ($Action -eq 'Plan' -or -not $Apply) { exit 0 }
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Service operations require an explicitly elevated test-machine session' }
    $existing = Get-CimInstance Win32_Service -Filter "Name='$name'"
    if ($Action -eq 'Install') {
        if ($existing) { throw 'Service already exists; refusing replacement' }
        if (Test-Path -LiteralPath $directory) { throw 'Installation directory exists; refusing takeover' }
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw 'Release executable not found' }
        New-Item -ItemType Directory -Path $directory | Out-Null
        $acl = [Security.AccessControl.DirectorySecurity]::new()
        $acl.SetAccessRuleProtection($true, $false)
        foreach ($entry in @(@('S-1-5-18', 'FullControl'), @('S-1-5-32-544', 'FullControl'), @('S-1-5-19', 'ReadAndExecute'))) {
            $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($entry[0]), $entry[1], 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
        }
        Set-Acl -LiteralPath $directory -AclObject $acl
        Copy-Item -LiteralPath $source -Destination $installed
        if ((Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -ne $hash) { throw 'Binary copy hash mismatch' }
        Invoke-ServiceTool @('create', $name, 'binPath=', $imagePath, 'start=', 'demand', 'obj=', 'NT AUTHORITY\LocalService')
        Write-Output 'Service installed, not started. SCM start/stop/restart and ACL acceptance require isolated machine evidence.'
    } else {
        if (-not $existing) { throw 'Service not found' }
        if ($existing.PathName -ne $imagePath -or $existing.StartName -ne 'NT AUTHORITY\LocalService') { throw 'Service ownership/image/account mismatch; refusing operation' }
        if ($existing.State -ne 'Stopped') {
            Invoke-ServiceTool @('stop', $name)
            (Get-Service -Name $name).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(10))
        }
        Invoke-ServiceTool @('delete', $name)
        Write-Output 'SCM entry removed; binary, policy and output data retained.'
    }
    exit 0
} catch { Write-Output "Service operation FAILED: $($_.Exception.Message). Files retained; no automatic system rollback claimed."; exit 1 }
