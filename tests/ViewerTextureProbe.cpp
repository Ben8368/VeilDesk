#include "graphics/D3D11Renderer.h"
#include "window/SecureWindow.h"
#include "security/DisplayProtection.h"
#include "broker/FrameReadback.h"
#include "common/Logger.h"
#include <iostream>

namespace veil {
struct D3D11RendererTestAccess {
    static ID3D11Texture2D* Snapshot(D3D11Renderer& renderer) { return renderer.source_.Get(); }
};
}
int wmain() {
    using namespace veil;
    using namespace veil::broker;
    Logger::Initialize(L"ViewerTextureProbe.log");
    SecureWindow window;
    if (!window.Create(GetModuleHandleW(nullptr)) || !DisplayProtection::Enable(window.Handle()).enabled) { return 1; }
    ZeroCopyStats displayStats, testStats;
    D3D11Renderer renderer;
    renderer.SetStats(&displayStats);
    if (!renderer.Initialize(window.Handle(), true, false, true)) { return 1; }
    window.Show(SW_SHOWNORMAL);
    SharedFrameProducer producer;
    SharedFrameConsumer consumer, wrongDevice;
    if (!producer.Initialize() || !consumer.Initialize(renderer.Device(), &displayStats) || !wrongDevice.Initialize()) { return 1; }
    FrameReadback testReadback;
    for (UINT size : {320U, 640U}) {
        Handle handle;
        if (!producer.Produce(size, handle, size, 180)) { return 1; }
        Message message; message.frame = size; message.pixelWidth = size;
        message.adapterLow = producer.Adapter().LowPart; message.adapterHigh = producer.Adapter().HighPart;
        HANDLE duplicate = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), handle.get(), GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            Logger::Win32Error(L"Viewer probe DuplicateHandle", GetLastError()); return 1;
        }
        message.handle = reinterpret_cast<std::uintptr_t>(duplicate);
        SharedTexture foreign;
        if (!wrongDevice.OpenFrame(message, foreign) || renderer.SetSourceTexture(foreign.Get()) || !foreign.Close()) { return 1; }
        message.handle = reinterpret_cast<std::uintptr_t>(handle.release());
        SharedTexture incoming;
        if (!consumer.OpenFrame(message, incoming) || !renderer.SetSourceTexture(incoming.Get()) || !incoming.Close()) { return 1; }
        std::vector<BYTE> pixels;
        // Readback exists only in this test binary, after Renderer finishes its GPU copy.
        if (!testReadback.Read(D3D11RendererTestAccess::Snapshot(renderer), pixels, &testStats) ||
            pixels != FramePixels(size, size, 180)) { Logger::Error(L"Renderer GPU snapshot pixel mismatch"); return 1; }
        if (!renderer.Resize(size + 100, 300) || !renderer.RenderFrame(window.Dpi())) { return 1; }
    }
    displayStats.Log();
    if (displayStats.cpuReadbacks || displayStats.cpuUploads || displayStats.texturesOpened != 2 || displayStats.gpuCopies < 4 ||
        testStats.cpuReadbacks != 2) { return 1; }
    std::wcout << L"ViewerTextureProbe passed: same-device GPU snapshot pixels, foreign device denied, source resize and presentation, display counters 0/0 (test-only readbacks=2)\n";
    return 0;
}
