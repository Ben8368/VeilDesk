param([string]$CargoPath = '', [string]$PublisherPin = '', [string]$TestImagePath = '', [switch]$Offline)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$previousPin = $env:VEILDESK_AUTHENTICODE_PUBLISHER_SHA256
$previousTestImage = $env:VEILDESK_AUTHENTICODE_TEST_IMAGE
try {
    if (-not $CargoPath) {
        $tool = Get-Command cargo -CommandType Application -ErrorAction SilentlyContinue
        $CargoPath = if ($tool) { $tool.Source } else { Join-Path $env:USERPROFILE '.cargo/bin/cargo.exe' }
    }
    if (-not (Test-Path -LiteralPath $CargoPath -PathType Leaf)) { throw 'Rust/Cargo unavailable; service verification not run' }
    if ($PublisherPin -and $PublisherPin -notmatch '^[0-9A-Fa-f]{64}$') { throw 'Publisher pin must be exactly 64 hexadecimal characters' }
    $argsList = @('--manifest-path', (Join-Path $root 'service/Cargo.toml'), '--locked')
    if ($Offline) { $argsList += '--offline' }
    $env:VEILDESK_AUTHENTICODE_PUBLISHER_SHA256 = $PublisherPin
    $testImage = ''
    if ($PublisherPin -and $TestImagePath -and (Test-Path -LiteralPath $TestImagePath -PathType Leaf)) {
        $signature = Get-AuthenticodeSignature -LiteralPath $TestImagePath
        if ($signature.Status -eq 'Valid' -and $signature.SignerCertificate) {
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $testImagePin = [BitConverter]::ToString($hasher.ComputeHash($signature.SignerCertificate.RawData)).Replace('-', '') }
            finally { $hasher.Dispose() }
            if ($testImagePin -eq $PublisherPin.ToUpperInvariant()) { $testImage = $TestImagePath }
        }
    }
    $env:VEILDESK_AUTHENTICODE_TEST_IMAGE = $testImage
    # Rust captures the same configured publisher pin as the C++ Broker at compile time.
    & $CargoPath test @argsList
    if ($LASTEXITCODE -ne 0) { throw 'Rust policy tests failed' }
    & $CargoPath build --release @argsList
    if ($LASTEXITCODE -ne 0) { throw 'Rust Release build failed' }
    & (Join-Path $root 'tests/service-smoke.ps1') -Executable (Join-Path $root 'service/target/release/veil-secure-service.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Service console IPC smoke failed' }
    Write-Output 'Service verification passed: policy, Release build and console IPC. SCM/installation not exercised.'
    $env:VEILDESK_AUTHENTICODE_PUBLISHER_SHA256 = $previousPin
    $env:VEILDESK_AUTHENTICODE_TEST_IMAGE = $previousTestImage
    exit 0
} catch {
    $env:VEILDESK_AUTHENTICODE_PUBLISHER_SHA256 = $previousPin
    $env:VEILDESK_AUTHENTICODE_TEST_IMAGE = $previousTestImage
    Write-Output "Service verification FAILED: $($_.Exception.Message)"
    exit 1
}
