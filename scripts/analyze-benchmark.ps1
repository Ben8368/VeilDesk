param([Parameter(Mandatory=$true)][string]$Csv, [Parameter(Mandatory=$true)][string]$OutputDir,
      [ValidateRange(0,7200)][int]$WarmupSeconds = 120, [ValidateRange(1,1800)][int]$BucketSeconds = 300)
$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
function Number($value) {
    $number = [double]::Parse([string]$value, $culture)
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt 0) { throw 'Invalid non-finite/negative resource value' }
    return $number
}
function Fit($points) {
    if ($points.Count -lt 2) { return $null }
    $origin = $points[0].x; $sx=0.0; $sy=0.0; $sxx=0.0; $sxy=0.0
    foreach ($point in $points) { $x=$point.x-$origin; $sx+=$x; $sy+=$point.y; $sxx+=$x*$x; $sxy+=$x*$point.y }
    $divisor = $points.Count*$sxx-$sx*$sx
    if ($divisor -le 0) { return $null }
    return ($points.Count*$sxy-$sx*$sy)/$divisor
}
$rows = @(Import-Csv -LiteralPath $Csv)
if ($rows.Count -lt 3) { throw 'Insufficient samples' }
foreach ($column in @('seconds','role','pid','produced_frames','displayed_frames','dropped_slots','private_bytes','handles','working_set_bytes','threads','cpu_percent_one_core','gpu_dedicated_bytes','gpu_shared_bytes','gpu_engine_max_percent')) {
    if ($rows[0].PSObject.Properties.Name -notcontains $column) { throw "Missing CSV column: $column" }
}
foreach ($row in $rows) { if (@('Broker','Producer','Viewer') -notcontains $row.role) { throw 'Unknown process role' } }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$fields = @('private_bytes','handles','working_set_bytes','threads','cpu_percent_one_core','gpu_dedicated_bytes','gpu_shared_bytes','gpu_engine_max_percent')
$reports = @(); $series = @()
foreach ($role in @('Broker','Producer','Viewer')) {
    $roleRows = @($rows | Where-Object { $_.role -eq $role })
    if ($roleRows.Count -lt 2) { throw "Missing process samples: $role" }
    if (@($roleRows.pid | Select-Object -Unique).Count -ne 1) { throw 'PID changed during benchmark' }
    $previousTime = -1.0; $previousFrames = 0.0
    foreach ($row in $roleRows) {
        $time = Number $row.seconds; $frames = Number $row.displayed_frames
        if ($time -le $previousTime -or $frames -lt $previousFrames) { throw 'Non-monotonic sample series' }
        if ((Number $row.produced_frames) -lt $frames -or (Number $row.pid) -eq 0 -or
            (Number $row.handles) -eq 0 -or (Number $row.threads) -eq 0 -or (Number $row.private_bytes) -eq 0) { throw 'Invalid process resource/frame data' }
        $previousTime = $time; $previousFrames = $frames
    }
    foreach ($field in $fields) {
        $points = @($roleRows | Where-Object { $_.$field -ne 'NOT AVAILABLE' } | ForEach-Object {
            @{ x=(Number $_.seconds); y=(Number $_.$field); frames=(Number $_.displayed_frames) }
        })
        $post = @($points | Where-Object { $_.x -ge $WarmupSeconds })
        $buckets = @()
        $count = [int][Math]::Floor((Number $roleRows[-1].seconds)/$BucketSeconds)
        for ($bucket=0; $bucket -lt $count; $bucket++) {
            $values = @($points | Where-Object { $_.x -ge $bucket*$BucketSeconds -and $_.x -lt ($bucket+1)*$BucketSeconds } | ForEach-Object { $_.y } | Sort-Object)
            if ($values.Count -eq 0) { continue }
            $buckets += @{ start_seconds=$bucket*$BucketSeconds; minimum=$values[0]; median=$values[[int][Math]::Floor($values.Count/2)]; maximum=$values[-1]; samples=$values.Count }
        }
        $slope = Fit $post
        $framePoints = @($post | ForEach-Object { @{x=$_.frames; y=$_.y} })
        $frameSlope = Fit $framePoints
        $range = if ($post.Count -gt 0) { ($post | ForEach-Object {$_.y} | Measure-Object -Maximum).Maximum - ($post | ForEach-Object {$_.y} | Measure-Object -Minimum).Minimum } else { $null }
        $increase = if ($post.Count -gt 1) { $post[-1].y-$post[0].y } else { $null }
        # Flags prompt investigation, never constitute an automatic no-leak/Gate PASS.
        $threshold = switch ($field) { 'handles' {16} 'threads' {2} 'private_bytes' {33554432} 'gpu_dedicated_bytes' {67108864} default {$null} }
        $flag = $null -ne $threshold -and $null -ne $increase -and $increase -gt $threshold -and $slope -gt 0
        $reports += @{ role=$role; metric=$field; available_samples=$points.Count; unavailable_samples=$roleRows.Count-$points.Count;
            post_warmup_samples=$post.Count; post_warmup_slope_per_second=$slope; slope_per_displayed_frame=$frameSlope;
            post_warmup_range=$range; post_warmup_first_to_last_delta=$increase; investigate_growth=$flag; buckets=$buckets }
        $series += @{ role=$role; metric=$field; points=$points }
    }
}
$report = @{ assessment='REVIEW REQUIRED'; warmup_seconds=$WarmupSeconds; bucket_seconds=$BucketSeconds;
    duration_seconds=(Number $rows[-1].seconds); metrics=$reports;
    limitations='WDDM counters may count shared allocations per process; unavailable values are not zero; endpoint or last-seconds stability is insufficient. Inspect all buckets and slopes. Object leaks below measurement sensitivity are not excluded.' }
$encoding = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $OutputDir 'trends.json'), ($report | ConvertTo-Json -Depth 8), $encoding)
$html = New-Object Text.StringBuilder
[void]$html.Append('<!doctype html><meta charset="utf-8"><title>VeilDesk resource trends</title><style>body{font:15px system-ui;background:#f5f6f8;color:#172134;margin:28px}section{background:white;padding:16px;margin:16px 0}svg{width:100%;max-width:960px;height:240px}polyline{fill:none;stroke:#206bbc;stroke-width:1.5}text{font:12px system-ui}h2{font-size:18px}</style><h1>VeilDesk resource trends — REVIEW REQUIRED</h1><p>Full run, 120s warmup for regression; CSV retains unavailable counters. Vertical scales differ by metric.</p>')
foreach ($item in $series) {
    [void]$html.Append('<section><h2>'+ $item.role +' / '+ $item.metric +'</h2>')
    if ($item.points.Count -lt 2) { [void]$html.Append('NOT AVAILABLE</section>'); continue }
    $min=($item.points | ForEach-Object {$_.y} | Measure-Object -Minimum).Minimum
    $max=($item.points | ForEach-Object {$_.y} | Measure-Object -Maximum).Maximum
    $span=[Math]::Max(1,$max-$min); $duration=[Math]::Max(1,$report.duration_seconds)
    $coordinates = @($item.points | ForEach-Object { (80+860*$_.x/$duration).ToString('F2',$culture)+','+(200-160*($_.y-$min)/$span).ToString('F2',$culture) }) -join ' '
    [void]$html.Append('<svg viewBox="0 0 960 240"><path d="M80 30V200H940" fill="none" stroke="#aaa"/><text x="0" y="40">'+$max+'</text><text x="0" y="200">'+$min+'</text><text x="80" y="225">0s</text><text x="880" y="225">'+$duration.ToString('F0',$culture)+'s</text><polyline points="'+$coordinates+'"/></svg></section>')
}
[IO.File]::WriteAllText((Join-Path $OutputDir 'curves.html'), $html.ToString(), $encoding)
$reports | ForEach-Object { [PSCustomObject]@{ Role=$_.role; Metric=$_.metric; Available=$_.available_samples; Unavailable=$_.unavailable_samples; Slope=$_.post_warmup_slope_per_second; Delta=$_.post_warmup_first_to_last_delta; Investigate=$_.investigate_growth } } | Format-Table -AutoSize
Write-Output "Analysis retained at $OutputDir; assessment requires review of the full run."
