param([string]$BuildDir, [string]$ResultDir)
$ErrorActionPreference = 'Stop'
$binary = Join-Path $BuildDir 'CaptureBroker.exe'
$suite = Join-Path $ResultDir ('benchmark-smoke-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $suite -Force | Out-Null
Push-Location $suite
try {
    foreach ($invalid in @(@('--duration-seconds','0'), @('--duration-seconds','7201'), @('--duration-seconds','-1'),
        @('--duration-seconds','999999999999999999999'), @('--duration-seconds','3junk'), @('--fps','0'), @('--fps','241'),
        @('--width','4097'), @('--width','215'), @('--height','35'), @('--sample-seconds','0'))) {
        & $binary --benchmark @invalid --output (Join-Path $suite 'invalid') | Out-Null
        if ($LASTEXITCODE -eq 0 -or (Test-Path -LiteralPath (Join-Path $suite 'invalid'))) { throw 'Invalid benchmark was admitted' }
    }
    & $binary --duration-seconds 3 --output (Join-Path $suite 'invalid') | Out-Null
    if ($LASTEXITCODE -eq 0) { throw 'Benchmark options without mode admitted' }
    & $binary --benchmark --duration-seconds 3 --width 320 --height 180 --fps 30 --sample-seconds 1 --output (Join-Path $suite 'metrics')
    if ($LASTEXITCODE -ne 0) { throw 'Parameterized benchmark failed' }
    $metrics = Get-Content (Join-Path $suite 'metrics/benchmark.json') -Raw | ConvertFrom-Json
    if ($metrics.duration_seconds -lt 3 -or $metrics.width -ne 320 -or $metrics.height -ne 180 -or
        $metrics.target_slots -ne 90 -or $metrics.frames_produced -ne $metrics.frames_displayed -or
        $metrics.frames_displayed -le 0 -or $metrics.frames_displayed+$metrics.frames_dropped -ne 90 -or
        $metrics.p99_delivery_ms -lt $metrics.p95_delivery_ms) { throw 'Invalid duration/dimensions/frame/latency measurements' }
    $rows = @(Import-Csv (Join-Path $suite 'metrics/resources.csv'))
    foreach ($role in @('Broker','Producer','Viewer')) {
        $samples = @($rows | Where-Object {$_.role -eq $role})
        if ($samples.Count -lt 3 -or @($samples.pid | Select-Object -Unique).Count -ne 1) { throw 'Missing stable process series' }
        foreach ($row in $samples) {
            if ([double]$row.private_bytes -le 0 -or [int]$row.handles -le 0 -or [int]$row.threads -le 0) { throw 'Invalid process measurement' }
        }
    }
    $viewer = Get-Content (Join-Path $suite 'SecureViewer.log') -Raw
    if ($viewer -notmatch 'CPU frame readbacks: 0' -or $viewer -notmatch 'CPU frame uploads: 0' -or
        $viewer -notmatch 'ResizeBuffers completed: 320x180') { throw 'Parameterized Viewer zero-copy mismatch' }
    $root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    & (Join-Path $root 'scripts/analyze-benchmark.ps1') -Csv (Join-Path $suite 'metrics/resources.csv') -OutputDir (Join-Path $suite 'analysis')
    Write-Output 'Benchmark smoke PASS: bounded options, real duration, process sampling, missing-counter semantics, parameterized GPU display.'
} finally { Pop-Location }
