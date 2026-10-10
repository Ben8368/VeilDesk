$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$rootPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$fixtureRoot = Join-Path $rootPath ('build/git-hook-fixtures/' + [guid]::NewGuid().ToString('N'))
$repoPath = Join-Path $fixtureRoot 'repo with spaces'
$utf8 = New-Object Text.UTF8Encoding($false)

function Invoke-Tool([string]$Executable, [string[]]$ToolArguments) {
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $Executable @ToolArguments 2>&1
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    return @{ Code = $code; Output = ($output -join "`n") }
}

function Invoke-FixtureGit([string[]]$ToolArguments) {
    return Invoke-Tool 'git' (@('-C', $repoPath) + $ToolArguments)
}

function Assert-Success($Result, [string]$Name) {
    if ($Result.Code -ne 0) { throw "$Name failed: $($Result.Output)" }
}

function Install-FixtureHooks {
    return Invoke-Tool 'powershell' @('-NoProfile', '-File',
        (Join-Path $rootPath 'scripts/install-git-hooks.ps1'), '-Root', $repoPath)
}

try {
    New-Item -ItemType Directory -Path (Join-Path $repoPath '.githooks') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $rootPath '.githooks/commit-msg') -Destination (Join-Path $repoPath '.githooks/commit-msg')
    Assert-Success (Invoke-FixtureGit @('init', '--quiet')) 'fixture init'
    foreach ($entry in @(@('user.name', 'Hook Fixture'), @('user.email', 'fixture@example.invalid'),
        @('commit.gpgsign', 'false'), @('core.autocrlf', 'false'))) {
        Assert-Success (Invoke-FixtureGit @('config', '--local', $entry[0], $entry[1])) 'fixture config'
    }
    Assert-Success (Invoke-FixtureGit @('config', '--local', 'core.hooksPath', 'existing-hooks')) 'custom hook config'
    if ((Install-FixtureHooks).Code -eq 0) { throw 'Installer overwrote a custom hooksPath' }
    if ((Invoke-FixtureGit @('config', '--local', '--get', 'core.hooksPath')).Output -ne 'existing-hooks') {
        throw 'Custom hooksPath changed after rejected installation'
    }
    Assert-Success (Invoke-FixtureGit @('config', '--local', '--unset', 'core.hooksPath')) 'clear fixture hooksPath'
    $existingHook = Join-Path $repoPath '.git/hooks/pre-commit'
    [IO.File]::WriteAllText($existingHook, "#!/bin/sh`nexit 0`n", $utf8)
    if ((Install-FixtureHooks).Code -eq 0) { throw 'Installer bypassed an existing native hook' }
    if ([IO.File]::ReadAllText($existingHook) -ne "#!/bin/sh`nexit 0`n") { throw 'Native hook was modified' }
    # Only delete the specific fixture file that this test just created.
    Remove-Item -LiteralPath $existingHook
    Assert-Success (Install-FixtureHooks) 'hook installation'
    Assert-Success (Install-FixtureHooks) 'idempotent hook installation'
    [IO.File]::WriteAllText((Join-Path $repoPath 'content.txt'), "fixture`n", $utf8)
    Assert-Success (Invoke-FixtureGit @('add', 'content.txt')) 'fixture staging'
    $codex = 'Co-authored-by: Codex <codex@openai.com>'
    $cursor = 'Co-authored-by: Cursor <cursoragent@cursor.com>'
    $claude = 'Co-authored-by: Claude Code <noreply@anthropic.com>'
    $cases = @(
        @{ Name = 'Cursor checkpoint'; Text = "checkpoint before checking out main`n"; Pass = $false },
        @{ Name = 'missing attribution'; Text = "fix: fixture`n"; Pass = $false },
        @{ Name = 'body mention'; Text = "fix: fixture`n`nUsed $codex in this explanation.`n"; Pass = $false },
        @{ Name = 'literal newline escapes'; Text = 'fix: fixture\n\n' + $codex; Pass = $false },
        @{ Name = 'wrong tool email'; Text = "fix: fixture`n`nCo-authored-by: Codex <wrong@example.invalid>`n"; Pass = $false },
        @{ Name = 'contradictory authorship'; Text = "fix: fixture`n`n$codex`nHuman-authored: true`n"; Pass = $false },
        @{ Name = 'Codex'; Text = "fix: fixture`n`n$codex`n"; Pass = $true },
        @{ Name = 'Cursor'; Text = "fix: fixture`n`n$cursor`n"; Pass = $true },
        @{ Name = 'Claude Code'; Text = "fix: fixture`n`n$claude`n"; Pass = $true },
        @{ Name = 'multiple tools'; Text = "fix: fixture`n`n$codex`n$cursor`n"; Pass = $true },
        @{ Name = 'CRLF message'; Text = "fix: fixture`r`n`r`n$cursor`r`n"; Pass = $true },
        @{ Name = 'explicit human'; Text = "fix: fixture`n`nHuman-authored: true`n"; Pass = $true }
    )
    $messagePath = Join-Path $fixtureRoot 'commit message.txt'
    foreach ($case in $cases) {
        [IO.File]::WriteAllText($messagePath, $case.Text, $utf8)
        $beforeHead = Invoke-FixtureGit @('rev-parse', '--verify', 'HEAD')
        $beforeTree = Invoke-FixtureGit @('write-tree')
        $result = Invoke-FixtureGit @('commit', '--allow-empty', '--cleanup=verbatim', '-F', $messagePath)
        if (($result.Code -eq 0) -ne $case.Pass) { throw "$($case.Name): unexpected outcome: $($result.Output)" }
        if ($case.Pass) {
            $stored = Invoke-FixtureGit @('show', '-s', '--format=%B', 'HEAD')
            if ($stored.Output.TrimEnd() -ne $case.Text.Replace("`r`n", "`n").TrimEnd()) {
                throw "$($case.Name): commit message or authorship was changed"
            }
        } else {
            if ($result.Output -notmatch 'VeilDesk:') { throw "$($case.Name): missing hook diagnostic" }
            $afterHead = Invoke-FixtureGit @('rev-parse', '--verify', 'HEAD')
            $afterTree = Invoke-FixtureGit @('write-tree')
            if ($beforeHead.Output -ne $afterHead.Output -or $beforeTree.Output -ne $afterTree.Output) {
                throw "$($case.Name): rejected commit changed HEAD or staged content"
            }
        }
        Write-Output "PASS: $($case.Name)"
    }
    Write-Output 'Git attribution hook regression PASSED (real commits in an isolated fixture).'
    exit 0
} catch {
    Write-Output "Git attribution hook regression FAILED: $($_.Exception.Message)"
    exit 1
}
