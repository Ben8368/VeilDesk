param([string]$CargoPath = '', [switch]$Offline)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
try {
    if (-not $CargoPath) {
        $tool = Get-Command cargo -CommandType Application -ErrorAction SilentlyContinue
        $CargoPath = if ($tool) { $tool.Source } else { Join-Path $env:USERPROFILE '.cargo/bin/cargo.exe' }
    }
    if (-not (Test-Path -LiteralPath $CargoPath -PathType Leaf)) { throw 'Rust/Cargo unavailable; service verification not run' }
    $argsList = @('--manifest-path', (Join-Path $root 'service/Cargo.toml'), '--locked')
    if ($Offline) { $argsList += '--offline' }
    & $CargoPath test @argsList
    if ($LASTEXITCODE -ne 0) { throw 'Rust policy tests failed' }
    & $CargoPath build --release @argsList
    if ($LASTEXITCODE -ne 0) { throw 'Rust Release build failed' }
    & (Join-Path $root 'tests/service-smoke.ps1') -Executable (Join-Path $root 'service/target/release/veil-secure-service.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Service console IPC smoke failed' }
    Write-Output 'Service verification passed: policy, Release build and console IPC. SCM/installation not exercised.'
    exit 0
} catch { Write-Output "Service verification FAILED: $($_.Exception.Message)"; exit 1 }
