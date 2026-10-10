param([string]$BuildDir = 'build/Release', [string]$ResultDir = 'build/test-results')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$binary = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$results = if ([IO.Path]::IsPathRooted($ResultDir)) { $ResultDir } else { Join-Path $root $ResultDir }
$suite = Join-Path $results ('zero-copy-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $suite -Force | Out-Null
Push-Location $suite
try {
    & (Join-Path $binary 'CaptureBroker.exe') --benchmark --output (Join-Path $suite 'metrics')
    if ($LASTEXITCODE -ne 0) { throw 'GPU benchmark failed' }
    $metrics = Get-Content -LiteralPath (Join-Path $suite 'metrics/benchmark.json') -Raw | ConvertFrom-Json
    $viewer = [IO.File]::ReadAllText((Join-Path $suite 'SecureViewer.log'))
    if ($metrics.duration_seconds -lt 60 -or $metrics.frames_displayed -le 0 -or
        $metrics.frames_displayed -ne $metrics.frames_produced -or
        ($metrics.frames_displayed + $metrics.frames_dropped) -ne 3600 -or
        $metrics.width -ne 1920 -or $metrics.height -ne 1080 -or $metrics.target_fps -ne 60) {
        throw 'Invalid benchmark measurements'
    }
    if ($viewer -notmatch 'CPU frame readbacks: 0(?:\r?\n)' -or
        $viewer -notmatch 'CPU frame uploads: 0(?:\r?\n)' -or
        $viewer -notmatch ('Shared textures opened: ' + $metrics.frames_displayed + '(?:\r?\n)') -or
        $viewer -notmatch 'ResizeBuffers completed: 1920x1080') { throw 'Benchmark Viewer zero-copy/size assertion failed' }
    Write-Output ($metrics | ConvertTo-Json)
    Write-Output "Benchmark completed: $suite. Latency measures frame request through Present/ACK, not physical scanout."
    Write-Output 'Performance and sampled resource trends require review; this is not a leak or display-protection certification.'
} finally { Pop-Location }
