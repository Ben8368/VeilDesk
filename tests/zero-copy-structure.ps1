param([string]$Root = (Join-Path $PSScriptRoot '..'))
$ErrorActionPreference = 'Stop'
$files = @('src/main.cpp', 'src/graphics/D3D11Renderer.cpp', 'src/graphics/D3D11Renderer.h',
    'src/broker/SharedFrame.h', 'src/broker/SharedFrame.cpp', 'src/broker/SharedFrameProducer.cpp')
foreach ($file in $files) {
    $text = [IO.File]::ReadAllText((Join-Path $Root $file))
    if ($text -match '\bMap\s*\(|D3D11_USAGE_STAGING|vector\s*<\s*BYTE\s*>|SetSourcePixels') {
        throw "CPU frame path reintroduced in $file"
    }
}
$cmake = [IO.File]::ReadAllText((Join-Path $Root 'CMakeLists.txt'))
if ($cmake -match 'target_link_libraries\(SecureViewer[^)]*VeilFrameReadback') {
    throw 'SecureViewer links Recorder readback library'
}
Write-Output 'Zero-copy structure passed: Viewer GPU path has no Map/staging/full CPU frame API; Recorder boundary remains separate.'
