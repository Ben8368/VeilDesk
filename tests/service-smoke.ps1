param([string]$Executable)
$ErrorActionPreference = 'Stop'
$result = Join-Path $PSScriptRoot ('results/service-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $result -Force | Out-Null
$process = $null
function Request([string]$Name, [byte[]]$Message) {
    $pipe = [IO.Pipes.NamedPipeClientStream]::new('.', $Name, [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous)
    try {
        $pipe.Connect(2000)
        $pipe.Write($Message, 0, $Message.Length)
        $buffer = New-Object byte[] 256
        $read = $pipe.ReadAsync($buffer, 0, $buffer.Length)
        if (-not $read.Wait(2000)) { throw 'IPC response timeout' }
        if ($read.Result -gt 0) {
            $receipt = [Text.Encoding]::UTF8.GetBytes("VD1 DONE`n")
            $pipe.Write($receipt, 0, $receipt.Length)
        }
        [Text.Encoding]::UTF8.GetString($buffer, 0, $read.Result)
    } finally { $pipe.Dispose() }
}
try {
    $process = Start-Process -FilePath $Executable -ArgumentList '--console', '8' -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $result 'stdout.log') -RedirectStandardError (Join-Path $result 'stderr.log')
    $processHandle = $process.Handle # Cache before exit; Windows PowerShell otherwise loses ExitCode.
    $name = 'VeilDesk.SecureService.test.' + $process.Id
    Start-Sleep -Milliseconds 300
    $status = Request $name ([Text.Encoding]::UTF8.GetBytes("VD1 STATUS`n"))
    if ($status -notmatch 'generation=(\d+).*grants=disabled') { throw "Unexpected status: $status" }
    $generation = [uint64]$Matches[1]
    $bad = Request $name ([Text.Encoding]::UTF8.GetBytes("VD2 STATUS`n"))
    if ($bad -notmatch 'DENIED') { throw 'Wrong protocol accepted' }
    $grant = Request $name ([Text.Encoding]::UTF8.GetBytes("VD1 GRANT 1`n"))
    if ($grant -notmatch 'DENIED') { throw 'Remote grant unexpectedly accepted' }
    $revoke = Request $name ([Text.Encoding]::UTF8.GetBytes("VD1 REVOKE`n"))
    if ($revoke -notmatch 'generation=(\d+)' -or [uint64]$Matches[1] -ne ($generation + [uint64]1)) { throw 'Revoke did not rotate generation' }
    $large = Request $name ([Text.Encoding]::UTF8.GetBytes(('A' * 129)))
    if ($large -and $large -notmatch 'DENIED') { throw 'Oversized request accepted' }
    if (-not $process.WaitForExit(10000)) { throw 'Console service exit timeout' }
    $process.Refresh()
    if ($process.ExitCode -ne 0) { throw "Console service failed: $($process.ExitCode)" }
    Write-Output 'Service console smoke passed: real pipe, protocol rejection, grants disabled, revoke, bounded lifetime.'
    exit 0
} catch { Write-Output "Service smoke FAILED: $($_.Exception.Message)"; exit 1 }
finally { if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id } }
