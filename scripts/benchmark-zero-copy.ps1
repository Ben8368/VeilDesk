param(
    [string]$BuildDir = 'build/Release', [string]$ResultDir = 'build/test-results',
    [ValidateRange(1, 7200)][int]$DurationSeconds = 1800,
    [ValidateRange(216, 4096)][int]$Width = 1920,
    [ValidateRange(36, 4096)][int]$Height = 1080,
    [ValidateRange(1, 240)][int]$Fps = 60,
    [ValidateRange(1, 60)][int]$SampleSeconds = 5
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$binary = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$results = if ([IO.Path]::IsPathRooted($ResultDir)) { $ResultDir } else { Join-Path $root $ResultDir }
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
Push-Location $suite
try {
    & (Join-Path $binary 'CaptureBroker.exe') --benchmark --duration-seconds $DurationSeconds --width $Width --height $Height --fps $Fps --sample-seconds $SampleSeconds --output (Join-Path $suite 'metrics')
    if ($LASTEXITCODE -ne 0) { throw 'GPU benchmark failed; partial CSV retained' }
    $metrics = Get-Content -LiteralPath (Join-Path $suite 'metrics/benchmark.json') -Raw | ConvertFrom-Json
    $viewer = [IO.File]::ReadAllText((Join-Path $suite 'SecureViewer.log'))
    if ($metrics.duration_seconds -lt $DurationSeconds -or $metrics.frames_displayed -le 0 -or
        $metrics.frames_displayed -ne $metrics.frames_produced -or
        ($metrics.frames_displayed + $metrics.frames_dropped) -ne ($DurationSeconds * $Fps) -or
        $metrics.width -ne $Width -or $metrics.height -ne $Height -or $metrics.target_fps -ne $Fps -or
        $metrics.p99_delivery_ms -lt $metrics.p95_delivery_ms) { throw 'Invalid benchmark measurements' }
    if ($viewer -notmatch 'CPU frame readbacks: 0(?:\r?\n)' -or
        $viewer -notmatch 'CPU frame uploads: 0(?:\r?\n)' -or
        $viewer -notmatch ('Shared textures opened: ' + $metrics.frames_displayed + '(?:\r?\n)') -or
        $viewer -notmatch ("ResizeBuffers completed: "+$Width+"x"+$Height)) { throw 'Benchmark Viewer zero-copy/size assertion failed' }
    Write-Output ($metrics | ConvertTo-Json)
    & (Join-Path $PSScriptRoot 'analyze-benchmark.ps1') -Csv (Join-Path $suite 'metrics/resources.csv') -OutputDir (Join-Path $suite 'analysis')
    if ($LASTEXITCODE -ne 0) { throw 'Resource trend analysis failed' }
    Write-Output "Benchmark completed: $suite. Latency measures frame request through Present/ACK, not physical scanout."
    Write-Output 'Resource trends require review; this is not a leak or display-protection certification.'
} finally { Pop-Location }
