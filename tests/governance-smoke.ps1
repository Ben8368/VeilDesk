$ErrorActionPreference = 'Stop'
$rootPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$checkerPath = Join-Path $rootPath 'scripts/governance-docs-check.ps1'
$config = [IO.File]::ReadAllText((Join-Path $rootPath 'scripts/governance-budgets.json'), [Text.Encoding]::UTF8) | ConvertFrom-Json
$utf8 = New-Object Text.UTF8Encoding($false)
# Fixtures contain source documentation only; no logs, screenshots or tools.
# Keep them in ignored build output for inspection, without recursive cleanup.
$fixture = Join-Path $rootPath ('build/governance-fixtures/' + [guid]::NewGuid().ToString('N'))
$fixtureFiles = @($config.requiredFiles) + @('scripts/governance-budgets.json')
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $rootPath 'docs') -Recurse -File -Filter '*.md') {
    $fixtureFiles += $file.FullName.Substring($rootPath.Length + 1).Replace('\', '/')
}
foreach ($relative in ($fixtureFiles | Sort-Object -Unique)) {
    $source = Join-Path $rootPath $relative
    $target = Join-Path $fixture $relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
}
& $checkerPath -Root $fixture
if ($LASTEXITCODE -ne 0) { throw 'Valid fixture rejected' }
$agentsPath = Join-Path $fixture 'AGENTS.md'
$original = [IO.File]::ReadAllText($agentsPath, $utf8)
$cases = @(
    @{ Name = 'broken local link'; Text = $original + "`n[broken](missing-file.md)`n" },
    @{ Name = 'entry budget'; Text = $original + (('x' * 100) + "`n") * 90 },
    @{ Name = 'CRLF'; Text = $original.Replace("`n", "`r`n") }
)
foreach ($case in $cases) {
    [IO.File]::WriteAllText($agentsPath, $case.Text, $utf8)
    $failureOutput = & $checkerPath -Root $fixture
    if ($LASTEXITCODE -eq 0) { throw "Invalid fixture accepted: $($case.Name)" }
    Write-Output "PASS: rejects $($case.Name)"
}
[IO.File]::WriteAllText($agentsPath, $original, $utf8)
& $checkerPath -Root $fixture
if ($LASTEXITCODE -ne 0) { throw 'Restored valid fixture rejected' }
Write-Output 'Governance checker regression PASSED.'
exit 0
