param(
    [string]$BuildDir = 'build',
    [string[]]$ConfigureArgs = @(),
    [switch]$SkipInteractive,
    [switch]$SkipService
)
$ErrorActionPreference = 'Stop'
$rootPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

function Invoke-CheckedTool([string]$Executable, [string[]]$ToolArguments) {
    & $Executable @ToolArguments
    if ($LASTEXITCODE -ne 0) { throw "Tool failed ($LASTEXITCODE): $Executable" }
}

try {
    & (Join-Path $PSScriptRoot 'governance-docs-check.ps1') -Root $rootPath
    if ($LASTEXITCODE -ne 0) { throw 'Governance validation failed' }
    & (Join-Path $rootPath 'tests/governance-smoke.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Governance checker regression failed' }
    & (Join-Path $rootPath 'tests/git-hooks-smoke.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Git attribution hook regression failed' }
    $cmakeCommand = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue
    if ($null -ne $cmakeCommand) { $cmakePath = $cmakeCommand.Source }
    else {
        $cmakePath = Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe'
        if (-not (Test-Path -LiteralPath $cmakePath)) { throw 'CMake not found; install CMake and reopen the terminal' }
    }
    $buildPath = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $rootPath $BuildDir }
    Invoke-CheckedTool $cmakePath (@('-S', $rootPath, '-B', $buildPath) + $ConfigureArgs)
    Invoke-CheckedTool $cmakePath @('--build', $buildPath, '--config', 'Release')
    if ($SkipService) { Write-Output 'SKIPPED: Rust service build, policy and console IPC verification.' }
    else {
        & (Join-Path $PSScriptRoot 'verify-service.ps1')
        if ($LASTEXITCODE -ne 0) { throw 'Service verification failed' }
    }
    $ctestPath = Join-Path (Split-Path $cmakePath -Parent) 'ctest.exe'
    if ($SkipInteractive) {
        Write-Output 'SKIPPED: Interactive Viewer, capture and shared GPU tests. Build/state tests are not capture evidence.'
        Invoke-CheckedTool $ctestPath @('--test-dir', $buildPath, '-C', 'Release', '--output-on-failure', '-LE', 'Interactive')
    } else {
        Invoke-CheckedTool $ctestPath @('--test-dir', $buildPath, '-C', 'Release', '--output-on-failure')
    }
    & (Join-Path $rootPath 'tests/workspace-smoke.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Workspace/service plan smoke failed' }
    Write-Output 'Verification completed within the reported scope.'
    exit 0
} catch {
    Write-Output "Verification FAILED: $($_.Exception.Message)"
    exit 1
}
