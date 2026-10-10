param([string]$OutputPath = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$result = [ordered]@{ SchemaVersion = 1; ReadOnly = $true; OS = $null; HypervisorPresent = $null; Cpu = @(); HyperVFeature = 'Unknown'; Vmms = 'Unknown'; HyperVCmdlets = $false; ReadyForVmOperations = $false; GuestTransport = 'NotImplemented'; Errors = @() }
try {
    $os = Get-CimInstance Win32_OperatingSystem
    $result.OS = [ordered]@{ Caption = $os.Caption; Version = $os.Version; Build = $os.BuildNumber; SKU = $os.OperatingSystemSKU }
} catch { $result.Errors += 'OS probe: ' + $_.Exception.Message }
try { $result.HypervisorPresent = (Get-CimInstance Win32_ComputerSystem).HypervisorPresent }
catch { $result.Errors += 'Hypervisor probe: ' + $_.Exception.Message }
try {
    $result.Cpu = @(Get-CimInstance Win32_Processor | Select-Object VirtualizationFirmwareEnabled, SecondLevelAddressTranslationExtensions, VMMonitorModeExtensions)
} catch { $result.Errors += 'CPU probe: ' + $_.Exception.Message }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    try { $result.HyperVFeature = [string](Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All).State }
    catch { $result.Errors += 'Feature probe: ' + $_.Exception.Message }
} else { $result.Errors += 'Feature query requires elevation; not run. CPU flags may be masked when a hypervisor is already present.' }
$service = Get-Service -Name vmms -ErrorAction SilentlyContinue
if ($service) { $result.Vmms = [string]$service.Status } else { $result.Vmms = 'NotFound' }
$result.HyperVCmdlets = $null -ne (Get-Command Get-VM -ErrorAction SilentlyContinue)
$result.ReadyForVmOperations = $result.HyperVCmdlets -and $result.Vmms -eq 'Running' -and $result.HyperVFeature -eq 'Enabled'
$json = $result | ConvertTo-Json -Depth 5
if ($OutputPath) {
    $absolute = [IO.Path]::GetFullPath($OutputPath)
    if (Test-Path -LiteralPath $absolute) { throw 'Probe output already exists; refusing overwrite' }
    [IO.File]::WriteAllText($absolute, $json.Replace("`r`n", "`n") + "`n", [Text.UTF8Encoding]::new($false))
}
Write-Output $json
