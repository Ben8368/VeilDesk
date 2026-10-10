param(
    [string]$BuildDir = 'build/Release', [string]$ResultDir = 'build/test-results',
    [string]$ServiceExe = 'service/target/release/veil-secure-service.exe',
    [ValidateRange(1, 50)][int]$DurationSeconds = 50,
    [ValidateRange(216, 4096)][int]$Width = 1920,
    [ValidateRange(36, 4096)][int]$Height = 1080,
    [ValidateRange(1, 240)][int]$Fps = 60,
    [ValidateRange(1, 60)][int]$SampleSeconds = 5
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$binary = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$results = if ([IO.Path]::IsPathRooted($ResultDir)) { $ResultDir } else { Join-Path $root $ResultDir }
$servicePath = if ([IO.Path]::IsPathRooted($ServiceExe)) { $ServiceExe } else { Join-Path $root $ServiceExe }
if (-not (Test-Path -LiteralPath (Join-Path $binary 'CaptureBroker.exe'))) { throw "CaptureBroker not found: $binary" }
if (-not (Test-Path -LiteralPath $servicePath)) { throw "SecureService not found: $servicePath" }
$suite = Join-Path $results ('zero-copy-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $suite -Force | Out-Null
$source = (& git -C $root rev-parse HEAD) -join ''
$dirty = @(& git -C $root status --porcelain).Count -ne 0
$context = & (Join-Path $PSScriptRoot 'benchmark-environment.ps1')
$environment = @{ source_commit = $source; dirty_worktree = $dirty; build_directory = $binary;
   desktop_context = $context; broker_sha256 = (Get-FileHash (Join-Path $binary 'CaptureBroker.exe') -Algorithm SHA256).Hash;
   viewer_sha256 = (Get-FileHash (Join-Path $binary 'SecureViewer.exe') -Algorithm SHA256).Hash;
   started_utc = [DateTime]::UtcNow.ToString('o'); os = [Environment]::OSVersion.VersionString;
   duration_seconds = $DurationSeconds; width = $Width; height = $Height; target_fps = $Fps }
[IO.File]::WriteAllText((Join-Path $suite 'environment.json'), ($environment | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
Write-Output "Benchmark artifacts: $suite"
$service = Start-Process -FilePath $servicePath -ArgumentList @('--console', '60') -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $suite 'service.stdout.log') -RedirectStandardError (Join-Path $suite 'service.stderr.log')
$authorizationOptions = @('--authorization-pipe', "\\.\pipe\VeilDesk.SecureService.test.$($service.Id).authorization",
    '--allow-console-service-peer', '--console-service-pid', [string]$service.Id)
Start-Sleep -Milliseconds 250
if ($service.HasExited) { throw "Console SecureService exited before benchmark start: $([IO.File]::ReadAllText((Join-Path $suite 'service.stderr.log')))" }
Push-Location $suite
try {
    & (Join-Path $binary 'CaptureBroker.exe') @authorizationOptions --benchmark --duration-seconds $DurationSeconds --width $Width --height $Height --fps $Fps --sample-seconds $SampleSeconds --output (Join-Path $suite 'metrics')
    if ($LASTEXITCODE -ne 0) { throw 'GPU benchmark failed; partial CSV retained' }
    $metrics = Get-Content -LiteralPath (Join-Path $suite 'metrics/benchmark.json') -Raw | ConvertFrom-Json
    $viewer = [IO.File]::ReadAllText((Join-Path $suite 'SecureViewer.log'))
    if ($metrics.duration_seconds -lt $DurationSeconds -or $metrics.frames_displayed -le 0 -or
        $metrics.frames_displayed -ne $metrics.frames_produced -or
        ($metrics.frames_displayed + $metrics.frames_dropped) -ne ($DurationSeconds * $Fps) -or
        $metrics.width -ne $Width -or $metrics.height -ne $Height -or $metrics.target_fps -ne $Fps -or
        $metrics.p99_delivery_ms -lt $metrics.p95_delivery_ms) { throw 'Invalid benchmark measurements' }
    if ($service.HasExited) { throw 'Console SecureService exited before the benchmark finished; metrics are not valid authorization-path evidence' }
    $brokerLogs = @(Get-ChildItem -LiteralPath $suite -Filter 'Broker-*.log' | Where-Object {
        [IO.File]::ReadAllText($_.FullName) -match 'Lease stopped; previous immutable snapshots remain readable; delivered='
    })
    if ($brokerLogs.Count -ne 1) { throw 'Expected exactly one completed Broker diagnostic log' }
    $brokerLog = [IO.File]::ReadAllText($brokerLogs[0].FullName)
    if ($brokerLog -match 'SecureService rejected frame authorization|ReadFile\(authorization response\) failed|lease renewal failed') {
        throw 'Broker logged an authorization-path failure; metrics are not valid G2-B performance evidence'
    }
    if ($viewer -notmatch 'CPU frame readbacks: 0(?:\r?\n)' -or
        $viewer -notmatch 'CPU frame uploads: 0(?:\r?\n)' -or
        $viewer -notmatch ('Shared textures opened: ' + $metrics.frames_displayed + '(?:\r?\n)') -or
        $viewer -notmatch ("ResizeBuffers completed: "+$Width+"x"+$Height)) { throw 'Benchmark Viewer zero-copy/size assertion failed' }
    Write-Output ($metrics | ConvertTo-Json)
    & (Join-Path $PSScriptRoot 'analyze-benchmark.ps1') -Csv (Join-Path $suite 'metrics/resources.csv') -OutputDir (Join-Path $suite 'analysis')
    if ($LASTEXITCODE -ne 0) { throw 'Resource trend analysis failed' }
    Write-Output "Benchmark completed: $suite. Latency measures frame request through Present/ACK, not physical scanout."
    Write-Output 'Resource trends require review; this is not a leak or display-protection certification.'
} finally {
    Pop-Location
    if ($null -ne $service -and -not $service.HasExited) { Stop-Process -Id $service.Id -Force }
    if ($null -ne $service) { $service.WaitForExit() }
}
