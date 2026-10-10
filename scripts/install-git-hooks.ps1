param([string]$Root = (Join-Path $PSScriptRoot '..'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

try {
    $rootPath = (Resolve-Path -LiteralPath $Root).Path
    $repoRoot = & git -C $rootPath rev-parse --show-toplevel
    if ($LASTEXITCODE -ne 0) { throw 'Root must be a Git worktree' }
    if ([IO.Path]::GetFullPath($repoRoot) -ne [IO.Path]::GetFullPath($rootPath)) {
        throw 'Root must be the repository root, not a subdirectory'
    }
    $hookPath = Join-Path $rootPath '.githooks/commit-msg'
    if (-not (Test-Path -LiteralPath $hookPath -PathType Leaf)) { throw 'Missing .githooks/commit-msg' }
    $configuredPath = & git -C $rootPath config --get core.hooksPath
    if ($LASTEXITCODE -notin @(0, 1)) { throw 'Cannot read core.hooksPath' }
    if ($configuredPath -and $configuredPath -ne '.githooks') {
        throw "Existing core.hooksPath is preserved: $configuredPath. Integrate hooks manually."
    }
    if (-not $configuredPath) {
        $defaultHooks = & git -C $rootPath rev-parse --git-path hooks
        if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the default hooks directory' }
        if (-not [IO.Path]::IsPathRooted($defaultHooks)) { $defaultHooks = Join-Path $rootPath $defaultHooks }
        if (Test-Path -LiteralPath $defaultHooks -PathType Container) {
            $activeHooks = @(Get-ChildItem -LiteralPath $defaultHooks -File -Force |
                Where-Object { $_.Name -notlike '*.sample' })
            if ($activeHooks.Count -gt 0) { throw 'Existing hooks are preserved. Integrate hooks manually.' }
        }
    }
    & git -C $rootPath config --local core.hooksPath .githooks
    if ($LASTEXITCODE -ne 0) { throw 'Cannot configure repository-local Git hooks' }
    Write-Output 'VeilDesk commit attribution hook enabled for this repository.'
    exit 0
} catch {
    Write-Output "Git hook installation FAILED: $($_.Exception.Message)"
    exit 1
}
