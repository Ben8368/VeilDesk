param([string]$BuildDir = "$PSScriptRoot\..\build\Release", [string[]]$ViewerArgs = @())
$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$process = Start-Process -FilePath "$buildPath\SecureViewer.exe" -ArgumentList (@('--smoke-test') + $ViewerArgs) -WorkingDirectory $buildPath -PassThru -WindowStyle Hidden
try {
    if (-not $process.WaitForExit(15000)) { throw 'Viewer smoke timeout' }
    if ($process.ExitCode -ne 0) { throw "Viewer failed with exit code $($process.ExitCode)" }
    $log = Get-Content -LiteralPath "$buildPath\SecureViewer.log" -Raw
    if ($log -notmatch 'Smoke window step 8 completed') { throw 'Window sequence incomplete' }
    if ($log -notmatch 'presented frames=[1-9][0-9]+') { throw 'Viewer did not sustain rendering' }
    if ($log -match '\[ERROR\]') { throw "Viewer logged an error: $log" }
    Write-Output $log
    Write-Output 'PASS: startup, resize, maximize, minimize/restore, borderless fullscreen, timed exit'
} finally {
    if (-not $process.HasExited) { [void]$process.CloseMainWindow(); [void]$process.WaitForExit(3000) }
    $process.Dispose()
}
