$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$folder = Join-Path $root ('tests/results/workspace-plan-' + [Guid]::NewGuid().ToString('N'))
try {
    $planText = & powershell -NoProfile -File (Join-Path $root 'scripts/workspace.ps1') -Action Create -Root $folder
    if ($LASTEXITCODE -ne 0) { throw 'Create preview failed' }
    $plan = ($planText -join "`n") | ConvertFrom-Json
    if ($plan.Apply -or $plan.Network -ne 'No virtual switch' -or $plan.GuestTransport -ne 'NotImplemented') { throw 'Unsafe/misleading VM plan defaults' }
    if (Test-Path -LiteralPath $folder) { throw 'Preview created files' }
    & powershell -NoProfile -File (Join-Path $root 'scripts/workspace.ps1') -Name '../OtherVm' -Apply
    if ($LASTEXITCODE -eq 0) { throw 'Path/name escape accepted' }
    & powershell -NoProfile -File (Join-Path $root 'scripts/workspace.ps1') -Root 'C:\'
    if ($LASTEXITCODE -eq 0) { throw 'Drive root accepted' }
    $serviceText = & powershell -NoProfile -File (Join-Path $root 'scripts/service-control.ps1') -Action Install
    if ($LASTEXITCODE -ne 0) { throw 'Service preview failed' }
    $service = ($serviceText -join "`n") | ConvertFrom-Json
    if ($service.Apply -or $service.StartAfterInstall -or $service.Account -ne 'NT AUTHORITY\LocalService') { throw 'Unsafe service plan defaults' }
    $probeText = & powershell -NoProfile -File (Join-Path $root 'scripts/workspace-probe.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Workspace probe failed' }
    $probe = ($probeText -join "`n") | ConvertFrom-Json
    if (-not $probe.ReadOnly -or $probe.SchemaVersion -ne 1) { throw 'Invalid probe schema' }
    Write-Output 'Workspace/service plan smoke passed; no VM/service/system changes exercised.'
    exit 0
} catch { Write-Output "Workspace smoke FAILED: $($_.Exception.Message)"; exit 1 }
