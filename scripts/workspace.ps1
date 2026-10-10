param(
    [ValidateSet('Plan', 'Create', 'Start', 'Stop', 'Checkpoint')] [string]$Action = 'Plan',
    [string]$Name = 'VeilDeskWorkspaceTest',
    [string]$Root = '',
    [string]$IsoPath = '',
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
try {
    if ($Name -notmatch '^VeilDesk[A-Za-z0-9-]{1,48}$') { throw 'Workspace name must have VeilDesk prefix and simple characters' }
    if (-not $Root) { $Root = Join-Path $env:ProgramData 'VeilDesk/Workspaces' }
    $absoluteRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if ($absoluteRoot -eq [IO.Path]::GetPathRoot($absoluteRoot).TrimEnd('\', '/')) { throw 'Drive root cannot be a workspace root' }
    $folder = [IO.Path]::GetFullPath((Join-Path $absoluteRoot $Name))
    if (-not $folder.StartsWith($absoluteRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Workspace path leaves root' }
    $manifest = Join-Path $folder 'workspace.json'
    $disk = Join-Path $folder 'system.vhdx'
    $plan = [ordered]@{ Action = $Action; Name = $Name; Folder = $folder; Generation = 2; MemoryBytes = 4GB; DiskBytes = 64GB; CpuCount = 2; Network = 'No virtual switch'; IsoPath = $IsoPath; EnhancedSession = 'Host policy unchanged; VMConnect is not a protected display path'; GuestTransport = 'NotImplemented'; DataRetention = 'No deletion or disk overwrite; failed creation retains data for inspection'; Apply = [bool]$Apply }
    $plan | ConvertTo-Json
    if ($Action -eq 'Plan' -or -not $Apply) { exit 0 }
    if (-not (Get-Command Get-VM -ErrorAction SilentlyContinue)) { throw 'Hyper-V management tools unavailable; run read-only workspace-probe.ps1' }
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'VM operations require an explicitly elevated test-machine session' }
    if ($Action -eq 'Create') {
        if (-not $IsoPath -or -not (Test-Path -LiteralPath $IsoPath -PathType Leaf) -or [IO.Path]::GetExtension($IsoPath) -ne '.iso') { throw 'Create requires an existing licensed Windows ISO' }
        if (Test-Path -LiteralPath $folder) { throw 'Workspace directory exists; refusing takeover/overwrite' }
        if (Get-VM -Name $Name -ErrorAction SilentlyContinue) { throw 'VM name already exists; refusing takeover' }
        New-Item -ItemType Directory -Path $folder | Out-Null
        $acl = [Security.AccessControl.DirectorySecurity]::new()
        $acl.SetAccessRuleProtection($true, $false)
        foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
            $rule = [Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid), 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
            $acl.AddAccessRule($rule)
        }
        Set-Acl -LiteralPath $folder -AclObject $acl
        $vm = New-VM -Name $Name -Generation 2 -MemoryStartupBytes 4GB -NewVHDPath $disk -NewVHDSizeBytes 64GB -Path $folder
        # Write ownership before subsequent configuration so partial failure is reviewable.
        $record = [ordered]@{ SchemaVersion = 1; VmId = [string]$vm.Id; Name = $Name; Disk = $disk; GuestTransport = 'NotImplemented' }
        [IO.File]::WriteAllText($manifest, ($record | ConvertTo-Json).Replace("`r`n", "`n") + "`n", [Text.UTF8Encoding]::new($false))
        Set-VM -VM $vm -ProcessorCount 2 -AutomaticCheckpointsEnabled $false -AutomaticStartAction Nothing -AutomaticStopAction ShutDown
        Set-VMFirmware -VM $vm -EnableSecureBoot On
        Add-VMDvdDrive -VM $vm -Path ([IO.Path]::GetFullPath($IsoPath))
        Set-VMFirmware -VM $vm -FirstBootDevice (Get-VMDvdDrive -VM $vm)
        Write-Output 'VM created with no virtual switch; OS installation, protected Guest display and boundary validation remain separate.'
        exit 0
    }
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw 'No VeilDesk ownership manifest; refusing to operate' }
    $record = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if ($record.SchemaVersion -ne 1 -or $record.Name -ne $Name -or $record.Disk -ne $disk) { throw 'Ownership manifest mismatch' }
    $vm = Get-VM -Id ([Guid]$record.VmId)
    if ($vm.Name -ne $Name) { throw 'VM identity mismatch' }
    if ($Action -eq 'Start') { Start-VM -VM $vm }
    elseif ($Action -eq 'Stop') { Stop-VM -VM $vm -Confirm:$false } # Graceful shutdown; never -TurnOff/-Force.
    elseif ($Action -eq 'Checkpoint') { Checkpoint-VM -VM $vm -SnapshotName ('VeilDesk-' + [Guid]::NewGuid().ToString('N')) }
    exit 0
} catch { Write-Output "Workspace FAILED: $($_.Exception.Message). Existing VM/disk data retained; inspect before recovery."; exit 1 }
