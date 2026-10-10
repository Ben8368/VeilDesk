param([string]$Root = (Join-Path $PSScriptRoot '..'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
$issues = New-Object 'System.Collections.Generic.List[string]'
$texts = @{}
$utf8 = New-Object System.Text.UTF8Encoding($false, $true)

function Read-CheckedText([string]$RelativePath) {
    $path = Join-Path $rootPath $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $issues.Add("Missing required file: $RelativePath")
        return $null
    }
    try {
        $bytes = [IO.File]::ReadAllBytes($path)
        $text = $utf8.GetString($bytes)
    } catch {
        $issues.Add("Cannot read strict UTF-8: $RelativePath")
        return $null
    }
    if ($text.Length -gt 0 -and [int]$text[0] -eq 0xFEFF) { $issues.Add("UTF-8 BOM is not allowed: $RelativePath") }
    if ($text.Contains("`r")) { $issues.Add("Use LF line endings: $RelativePath") }
    if (-not $text.EndsWith("`n")) { $issues.Add("Missing final newline: $RelativePath") }
    $texts[$RelativePath.Replace('\', '/')] = $text
    return $text
}

$configText = Read-CheckedText 'scripts/governance-budgets.json'
try { $config = $configText | ConvertFrom-Json } catch { Write-Output 'Invalid governance-budgets.json'; exit 1 }
if ($null -eq $config) { Write-Output ($issues -join "`n"); exit 1 }
foreach ($file in $config.requiredFiles) { [void](Read-CheckedText $file) }

# Explicit source roots avoid traversing local tools, build output or captures.
foreach ($directory in @('docs', 'scripts', '.github')) {
    $path = Join-Path $rootPath $directory
    if (Test-Path -LiteralPath $path -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $path -Recurse -File) {
            if ($file.Extension -in @('.md', '.ps1', '.json', '.yml', '.yaml')) {
                $relative = $file.FullName.Substring($rootPath.Length + 1).Replace('\', '/')
                if (-not $texts.ContainsKey($relative)) { [void](Read-CheckedText $relative) }
            }
        }
    }
}
foreach ($file in Get-ChildItem -LiteralPath $rootPath -File -Filter '*.md') {
    if (-not $texts.ContainsKey($file.Name)) { [void](Read-CheckedText $file.Name) }
}
$testDirectory = Join-Path $rootPath 'tests'
if (Test-Path -LiteralPath $testDirectory) {
    foreach ($file in Get-ChildItem -LiteralPath $testDirectory -File -Filter '*.ps1') {
        $relative = 'tests/' + $file.Name
        if (-not $texts.ContainsKey($relative)) { [void](Read-CheckedText $relative) }
    }
}

foreach ($budget in $config.budgets) {
    if (-not $texts.ContainsKey($budget.file)) { continue }
    $content = $texts[$budget.file]
    $lines = [regex]::Split($content.TrimEnd(), "`n").Length
    if ($lines -gt $budget.lines -or $content.Length -gt $budget.characters) {
        $issues.Add("Budget exceeded: $($budget.file) lines=$lines/$($budget.lines) chars=$($content.Length)/$($budget.characters)")
    }
}

foreach ($entry in $texts.GetEnumerator()) {
    if (-not $entry.Key.EndsWith('.md')) { continue }
    # The checker validates file targets, not generated Markdown heading anchors.
    foreach ($match in [regex]::Matches($entry.Value, '\[[^\]\r\n]+\]\((?<target>[^)\r\n]+)\)')) {
        $link = $match.Groups['target'].Value.Trim().Trim('<', '>')
        if ($link -match '^(https?:|mailto:|#)') { continue }
        $target = [Uri]::UnescapeDataString(($link -split '#', 2)[0])
        if ([string]::IsNullOrWhiteSpace($target)) { continue }
        $source = Join-Path $rootPath $entry.Key
        try { $resolved = [IO.Path]::GetFullPath((Join-Path (Split-Path $source -Parent) $target)) }
        catch { $issues.Add("Invalid local link: $($entry.Key) -> $link"); continue }
        if (-not $resolved.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            $issues.Add("Link leaves repository: $($entry.Key) -> $link")
            continue
        }
        $relative = $resolved.Substring($rootPath.Length + 1).Replace('\', '/')
        if ($relative -match '^(\.git|\.tools|\.vs|build[^/]*|artifacts|visual-evidence|tests/results)(/|$)') {
            $issues.Add("Link points to local-only output: $($entry.Key) -> $link")
        } elseif (-not (Test-Path -LiteralPath $resolved)) {
            $issues.Add("Broken local link: $($entry.Key) -> $link")
        }
    }
}

if ($texts.ContainsKey('CONTEXT.md')) {
    $contextText = $texts['CONTEXT.md']
    if ($contextText -notmatch '\*\*\u9636\u6BB5\uFF1A\*\*\s*Phase\s+([1-4])') { $issues.Add('CONTEXT.md: missing explicit current Phase') }
    $priorities = [regex]::Match($contextText, '(?ms)^## \u8FD1\u671F\u4F18\u5148\u7EA7\n(.*?)(?=^## |\z)').Groups[1].Value
    $numbers = @([regex]::Matches($priorities, '(?m)^([1-3])\. ') | ForEach-Object { $_.Groups[1].Value })
    if (($numbers -join ',') -ne '1,2,3') { $issues.Add('CONTEXT.md: keep exactly three numbered near-term priorities') }
}
foreach ($summary in @('CLAUDE.md', '.cursorrules')) {
    if ($texts.ContainsKey($summary) -and -not $texts[$summary].Contains('AGENTS.md')) { $issues.Add("$summary must route to AGENTS.md") }
}
if ($texts.ContainsKey('docs/ROADMAP.md')) {
    foreach ($phase in @(2, 3, 4)) {
        if ($texts['docs/ROADMAP.md'] -notmatch "(?m)^## Phase $phase(\uFF1A|:)") { $issues.Add("Roadmap missing Phase $phase") }
    }
    foreach ($gate in @(1, 2, 3, 4)) {
        if ($texts['docs/ROADMAP.md'] -notmatch "(?m)^### Gate G$gate\b") { $issues.Add("Roadmap missing Gate G$gate") }
    }
}

$adrCount = 0
foreach ($entry in $texts.GetEnumerator()) {
    if ($entry.Key -notmatch '^docs/ADR/\d{4}-.+\.md$') { continue }
    $adrCount++
    if ($entry.Value -notmatch '(?m)^- \u72B6\u6001\uFF1A(\u63D0\u8BAE|\u5DF2\u63A5\u53D7|\u5DF2\u66FF\u4EE3|\u5DF2\u62D2\u7EDD)$') {
        $issues.Add("ADR missing valid status: $($entry.Key)")
    }
    $dateMatch = [regex]::Match($entry.Value, '(?m)^- \u65E5\u671F\uFF1A(\d{4}-\d{2}-\d{2})$')
    $parsedDate = [datetime]::MinValue
    if (-not $dateMatch.Success -or -not [datetime]::TryParseExact($dateMatch.Groups[1].Value, 'yyyy-MM-dd',
        [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$parsedDate)) {
        $issues.Add("ADR missing valid ISO date: $($entry.Key)")
    }
    foreach ($heading in @('\u80CC\u666F', '\u51B3\u7B56', '\u540E\u679C', '\u66FF\u4EE3\u65B9\u6848', '\u5173\u8054\u6587\u6863')) {
        if ($entry.Value -notmatch "(?m)^## $heading`$") { $issues.Add("ADR missing required section: $($entry.Key) / $heading") }
    }
}
if ($adrCount -eq 0) { $issues.Add('No numbered ADR records found') }
if ($issues.Count -gt 0) {
    Write-Output 'Governance check FAILED:'
    foreach ($issue in $issues) { Write-Output "- $issue" }
    exit 1
}
Write-Output "Governance check PASSED: $($texts.Count) text files, $adrCount ADRs; UTF-8/LF, budgets, local links, routes, priorities and phases."
exit 0
