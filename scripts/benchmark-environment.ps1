# Read-only context; no session unlock, service install, policy change or window manipulation.
$ErrorActionPreference = 'Stop'
if (-not ('VeilDeskBenchmarkEnvironment' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class VeilDeskBenchmarkEnvironment {
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool SystemParametersInfoW(uint action, uint parameter, out bool running, uint flags);
    [DllImport("wtsapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool WTSQuerySessionInformationW(IntPtr server, uint session, int type, out IntPtr buffer, out uint bytes);
    [DllImport("wtsapi32.dll")] public static extern void WTSFreeMemory(IntPtr buffer);
}
'@
}
$session = [uint32][Diagnostics.Process]::GetCurrentProcess().SessionId
$buffer=[IntPtr]::Zero; $bytes=[uint32]0
$state='NOT AVAILABLE'; $queryError=$null; $connectionState=$null
if ([VeilDeskBenchmarkEnvironment]::WTSQuerySessionInformationW([IntPtr]::Zero,$session,25,[ref]$buffer,[ref]$bytes)) {
    try {
        # WTSINFOEX Level 1: DWORD Level, 8-byte-aligned union; only fixed numeric fields are read.
        # Do not read embedded user/domain/WinStation strings.
        if ([IntPtr]::Size -eq 8 -and $bytes -ge 20 -and [Runtime.InteropServices.Marshal]::ReadInt32($buffer,0) -eq 1 -and
            [Runtime.InteropServices.Marshal]::ReadInt32($buffer,8) -eq $session) {
            $connectionState=[Runtime.InteropServices.Marshal]::ReadInt32($buffer,12)
            $sessionFlags=[Runtime.InteropServices.Marshal]::ReadInt32($buffer,16)
            if ([Environment]::OSVersion.Version -ge [Version]'6.2') {
                if ($sessionFlags -eq 0) { $state='LOCKED' } elseif ($sessionFlags -eq 1) { $state='UNLOCKED' }
            }
        }
    } finally { [VeilDeskBenchmarkEnvironment]::WTSFreeMemory($buffer) }
} else { $queryError=[Runtime.InteropServices.Marshal]::GetLastWin32Error() }
$running=$false; $screensaver='NOT AVAILABLE'
if ([VeilDeskBenchmarkEnvironment]::SystemParametersInfoW(0x72,0,[ref]$running,0)) { $screensaver=$running }
[PSCustomObject]@{ observed_utc=[DateTime]::UtcNow.ToString('o'); session_id=$session; session_lock_state=$state;
    connection_state=$connectionState; wts_query_error=$queryError; screensaver_running=$screensaver }
