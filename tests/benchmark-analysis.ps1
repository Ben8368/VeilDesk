param([string]$ResultDir = (Join-Path $PSScriptRoot '../build/test-results'))
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$analyzer = Join-Path $root 'scripts/analyze-benchmark.ps1'
$suite = Join-Path $ResultDir ('analysis-fixtures-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $suite -Force | Out-Null
# Synthetic fixtures test the analysis, never count as runtime resource evidence.
foreach ($mode in @('flat','linear')) {
    $rows = @()
    foreach ($time in 0..360) {
        foreach ($role in @('Broker','Producer','Viewer')) {
            $growth = if ($mode -eq 'linear') { $time } else { 0 }
            $rows += [PSCustomObject]@{seconds=$time*5; role=$role; pid=100+([array]::IndexOf(@('Broker','Producer','Viewer'),$role));
                produced_frames=$time*300; displayed_frames=$time*300; dropped_slots=0;
                private_bytes=50000000+$growth*1048576; handles=100+$growth; working_set_bytes=40000000+$growth*1048576;
                threads=10+$growth; cpu_percent_one_core='NOT AVAILABLE'; gpu_dedicated_bytes=20000000+$growth*1048576;
                gpu_shared_bytes='NOT AVAILABLE'; gpu_engine_max_percent='NOT AVAILABLE'}
        }
    }
    $csv = Join-Path $suite ($mode+'.csv'); $rows | Export-Csv -LiteralPath $csv -NoTypeInformation
    $out = Join-Path $suite $mode
    & $analyzer -Csv $csv -OutputDir $out | Out-Null
    $report = Get-Content (Join-Path $out 'trends.json') -Raw | ConvertFrom-Json
    if ($report.assessment -ne 'REVIEW REQUIRED') { throw 'Analysis incorrectly certifies a Gate' }
    foreach ($role in @('Broker','Producer','Viewer')) {
        foreach ($metric in @('private_bytes','handles','threads','gpu_dedicated_bytes')) {
            $result = @($report.metrics | Where-Object {$_.role -eq $role -and $_.metric -eq $metric})[0]
            if ($result.investigate_growth -ne ($mode -eq 'linear') -or $result.buckets.Count -ne 6) { throw 'Full-window growth detection failed' }
            if ($metric -eq 'handles' -and [Math]::Abs($result.post_warmup_slope_per_second - $(if ($mode -eq 'linear') {0.2} else {0})) -gt 0.00001) { throw 'Regression slope incorrect' }
        }
        $missing = @($report.metrics | Where-Object {$_.role -eq $role -and $_.metric -eq 'gpu_shared_bytes'})[0]
        if ($missing.available_samples -ne 0 -or $missing.unavailable_samples -ne 361 -or $null -ne $missing.post_warmup_slope_per_second) { throw 'Unavailable value became zero' }
    }
}
$flat = @(Import-Csv (Join-Path $suite 'flat.csv'))
foreach ($case in @('nan','negative','pid-change','time-reversal','unknown-role','missing-field')) {
    $rows = $flat | Select-Object *
    switch ($case) {
        'nan' { $rows[10].private_bytes='NaN' }
        'negative' { $rows[10].private_bytes=-1 }
        'pid-change' { $rows[10].pid=999 }
        'time-reversal' { $rows[10].seconds=0 }
        'unknown-role' { $rows[10].role='Attacker' }
        'missing-field' { $rows = $rows | Select-Object -Property * -ExcludeProperty threads }
    }
    $csv = Join-Path $suite ($case+'.csv'); $rows | Export-Csv -LiteralPath $csv -NoTypeInformation
    $denied = $false
    try { & $analyzer -Csv $csv -OutputDir (Join-Path $suite $case) | Out-Null } catch { $denied=$true }
    if (!$denied) { throw "Invalid resource series admitted: $case" }
}
Write-Output 'Benchmark analysis PASS: linear leak fixtures flagged, all six windows inspected, missing data preserved, malformed/PID-changing/non-monotonic series rejected. Synthetic data only.'
