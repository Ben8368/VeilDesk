param(
    [string]$BuildDir = "$PSScriptRoot\..\build\Release",
    [string]$ResultDir = "$PSScriptRoot\results"
)
$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
New-Item -ItemType Directory -Path $ResultDir -Force | Out-Null
$resultsPath = (Resolve-Path -LiteralPath $ResultDir).Path
$cases = @(
    @{ Name = 'baseline'; Args = @('--no-protection'); Expect = '--expect-visible' },
    @{ Name = 'protected'; Args = @(); Expect = '--expect-excluded' },
    @{ Name = 'wda-only'; Args = @('--disable-display-only'); Expect = '--expect-excluded' },
    @{ Name = 'fallback'; Args = @('--simulate-display-only-failure'); Expect = '--expect-excluded' }
)
foreach ($case in $cases) {
    $viewerArgs = @('--run-seconds', '8', '--capture-ready') + $case.Args
    $viewer = Start-Process -FilePath "$buildPath\SecureViewer.exe" -ArgumentList $viewerArgs -WorkingDirectory $buildPath -PassThru -WindowStyle Hidden
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 100
            $ready = $false
            if (Test-Path -LiteralPath "$buildPath\SecureViewer.log") {
                $startupLog = Get-Content -LiteralPath "$buildPath\SecureViewer.log" -Raw
                $ready = ($startupLog -match "Process PID=$($viewer.Id);") -and ($startupLog -match 'Presented frame 1')
            }
            if ($viewer.HasExited) { throw 'Viewer exited during initialization' }
        } while (-not $ready -and [DateTime]::UtcNow -lt $deadline)
        if (-not $ready) { throw 'Viewer readiness timeout' }
        Start-Sleep -Milliseconds 600
        Push-Location $buildPath
        try {
            & "$buildPath\CaptureTest.exe" --pid $viewer.Id --output "$resultsPath\$($case.Name).png" $case.Expect
            if ($LASTEXITCODE -ne 0) { throw "Capture $($case.Name) failed: $LASTEXITCODE" }
        } finally { Pop-Location }
        if (-not $viewer.WaitForExit(12000)) { throw 'Viewer timeout' }
        if ($viewer.ExitCode -ne 0) { throw "Viewer failed: $($viewer.ExitCode)" }
        $viewerLog = Get-Content -LiteralPath "$buildPath\SecureViewer.log" -Raw
        if ($viewerLog -match '\[ERROR\]') { throw "Viewer errors: $viewerLog" }
        if ($case.Name -ne 'baseline' -and $viewerLog -notmatch 'WDA_EXCLUDEFROMCAPTURE enabled') { throw 'WDA API confirmation missing' }
        if ($case.Name -eq 'fallback' -and $viewerLog -notmatch 'fallback enabled') { throw 'Fallback did not execute' }
        Copy-Item -LiteralPath "$buildPath\SecureViewer.log" -Destination "$resultsPath\$($case.Name)-viewer.log" -Force
        Copy-Item -LiteralPath "$buildPath\CaptureTest.log" -Destination "$resultsPath\$($case.Name)-capture.log" -Force
        Write-Output "PASS: $($case.Name) (PNG/marker evidence; manual inspection still required)"
    } finally {
        if (-not $viewer.HasExited) { [void]$viewer.CloseMainWindow(); [void]$viewer.WaitForExit(3000) }
        $viewer.Dispose()
    }
}
