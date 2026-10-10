param(
    [string]$BuildDir = 'build',
    [string[]]$ConfigureArgs = @(),
    [switch]$SkipInteractive
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
    $cmakeCommand = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue
    if ($null -ne $cmakeCommand) { $cmakePath = $cmakeCommand.Source }
    else {
        $cmakePath = Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe'
        if (-not (Test-Path -LiteralPath $cmakePath)) { throw 'CMake not found; install CMake and reopen the terminal' }
    }
    $buildPath = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $rootPath $BuildDir }
    Invoke-CheckedTool $cmakePath (@('-S', $rootPath, '-B', $buildPath) + $ConfigureArgs)
    Invoke-CheckedTool $cmakePath @('--build', $buildPath, '--config', 'Release')
    if ($SkipInteractive) {
        Write-Output 'SKIPPED: WindowSmoke and CaptureSmoke require an interactive Windows desktop. Build-only is not capture evidence.'
    } else {
        $ctestPath = Join-Path (Split-Path $cmakePath -Parent) 'ctest.exe'
        Invoke-CheckedTool $ctestPath @('--test-dir', $buildPath, '-C', 'Release', '--output-on-failure')
    }
    Write-Output 'Verification completed within the reported scope.'
    exit 0
} catch {
    Write-Output "Verification FAILED: $($_.Exception.Message)"
    exit 1
}
