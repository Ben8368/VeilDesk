param([string]$BuildDir, [string]$ResultDir)
$ErrorActionPreference = 'Stop'
$broker = Join-Path $BuildDir 'CaptureBroker.exe'
$suite = Join-Path $ResultDir ('broker-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $suite -Force | Out-Null
function Run-Case([string]$Name, [string[]]$Options, [int]$ExpectedExit, [int]$ExpectedFrames) {
    $path = Join-Path $suite $Name
    & $broker --output $path @Options
    if ($LASTEXITCODE -ne $ExpectedExit) { throw "$Name exit=$LASTEXITCODE expected=$ExpectedExit" }
    $count = @(Get-ChildItem -LiteralPath $path -Filter 'frame-*.png' -ErrorAction SilentlyContinue).Count
    if ($count -ne $ExpectedFrames) { throw "$Name frames=$count expected=$ExpectedFrames" }
    if (@(Get-ChildItem -LiteralPath $path -Filter 'pending-*.png').Count -ne 0) { throw "$Name partial output remains" }
    Add-Type -AssemblyName System.Drawing
    foreach ($file in Get-ChildItem -LiteralPath $path -Filter 'frame-*.png') {
        $bitmap = [Drawing.Bitmap]::FromFile($file.FullName)
        try {
            if ($bitmap.Width -ne 320 -or $bitmap.Height -ne 180) { throw 'PNG dimensions incorrect' }
            $frame = [int]($file.BaseName -replace '^frame-', '')
            $pixel = $bitmap.GetPixel(0, 0)
            if ($pixel.B -ne $frame -or $pixel.R -ne 0 -or $pixel.G -ne 0) { throw 'Independent PNG frame ID mismatch' }
        } finally { $bitmap.Dispose() }
    }
}
Push-Location $suite
try {
    Run-Case 'approved' @('--frames', '8', '--viewer') 0 8
    $viewerLog = [IO.File]::ReadAllText((Join-Path $suite 'SecureViewer.log'))
    if ($viewerLog -notmatch 'CPU frame readbacks: 0(?:\r?\n)' -or
        $viewerLog -notmatch 'CPU frame uploads: 0(?:\r?\n)' -or
        $viewerLog -notmatch 'Shared textures opened: 8(?:\r?\n)' -or
        $viewerLog -notmatch 'GPU copies: [1-9][0-9]*') { throw 'Viewer zero-copy instrumentation failed' }
    Write-Output 'Viewer ZeroCopyStats verified: readbacks=0 uploads=0 opens=8 GPU copies>0.'
    Run-Case 'revoked' @('--frames', '8', '--revoke-after', '2') 0 2
    Run-Case 'expired' @('--expired') 0 0
    Run-Case 'denied' @('--deny') 0 0
    Run-Case 'producer-exits' @('--producer-exit') 1 0
    Run-Case 'consumer-exits' @('--consumer-exit') 1 0
    Run-Case 'stalled' @('--stall') 1 0
    & $broker --consumer --output $suite
    if ($LASTEXITCODE -eq 0) { throw 'Unattached consumer accepted' }
    & $broker --producer
    if ($LASTEXITCODE -eq 0) { throw 'Unattached producer accepted' }
    & $broker --output (Join-Path $suite 'approved')
    if ($LASTEXITCODE -eq 0) { throw 'Existing output directory overwritten' }
    Write-Output 'Broker smoke passed: GPU pixel validation, Viewer, PNG sequence, revoke, expiry, deny, timeout, unattached rejection.'
} finally { Pop-Location }
