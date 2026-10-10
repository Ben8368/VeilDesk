#include "capture/CaptureEngine.h"
#include "common/Logger.h"
#include "common/CapturePattern.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <limits>

namespace {
class ScreenDC final {
public:
    ScreenDC() : handle(GetDC(nullptr)) {}
    ~ScreenDC() { if (handle && !ReleaseDC(nullptr, handle)) { veil::Logger::Win32Error(L"ReleaseDC", GetLastError()); } }
    ScreenDC(const ScreenDC&) = delete;
    ScreenDC& operator=(const ScreenDC&) = delete;
    HDC handle;
};
class MemoryDC final {
public:
    explicit MemoryDC(HDC source) : handle(CreateCompatibleDC(source)) {}
    ~MemoryDC() { if (handle && !DeleteDC(handle)) { veil::Logger::Win32Error(L"DeleteDC", GetLastError()); } }
    MemoryDC(const MemoryDC&) = delete;
    MemoryDC& operator=(const MemoryDC&) = delete;
    HDC handle;
};
class DibSelection final {
public:
    DibSelection(HDC dc, int width, int height) : dc_(dc) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap_ = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap_) { veil::Logger::Win32Error(L"CreateDIBSection", GetLastError()); return; }
        old_ = SelectObject(dc, bitmap_);
        if (!old_ || old_ == HGDI_ERROR) { veil::Logger::Win32Error(L"SelectObject", GetLastError()); }
    }
    ~DibSelection() {
        if (old_ && old_ != HGDI_ERROR) {
            const HGDIOBJ restored = SelectObject(dc_, old_);
            if (!restored || restored == HGDI_ERROR) { veil::Logger::Win32Error(L"SelectObject(restore)", GetLastError()); }
        }
        if (bitmap_ && !DeleteObject(bitmap_)) { veil::Logger::Win32Error(L"DeleteObject(bitmap)", GetLastError()); }
    }
    DibSelection(const DibSelection&) = delete;
    DibSelection& operator=(const DibSelection&) = delete;
    bool Valid() const { return bitmap_ && bits && old_ && old_ != HGDI_ERROR; }
    void* bits = nullptr;
private:
    HDC dc_;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
};
}
namespace veil {
bool CaptureEngine::CaptureClient(HWND hwnd, CapturedImage& image) {
    image = {};
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        Logger::Error(L"Selected window must be valid, visible and not minimized; result is inconclusive"); return false;
    }
    RECT client{};
    if (!GetClientRect(hwnd, &client)) { Logger::Win32Error(L"GetClientRect", GetLastError()); return false; }
    POINT first{client.left, client.top};
    POINT last{client.right, client.bottom};
    if (!ClientToScreen(hwnd, &first) || !ClientToScreen(hwnd, &last)) {
        Logger::Win32Error(L"ClientToScreen", GetLastError()); return false;
    }
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int width = last.x - first.x;
    const int height = last.y - first.y;
    const auto bytes = static_cast<unsigned long long>(width > 0 ? width : 0) * static_cast<unsigned long long>(height > 0 ? height : 0) * 4ULL;
    if (width <= 0 || height <= 0 || bytes > 512ULL * 1024 * 1024 ||
        first.x < left || first.y < top || last.x > right || last.y > bottom) {
        Logger::Error(L"Client rectangle is empty, too large or partly outside the virtual screen; result is inconclusive"); return false;
    }
    Logger::Info(L"GDI screen BitBlt client rectangle: " + std::to_wstring(first.x) + L"," + std::to_wstring(first.y) +
        L" " + std::to_wstring(width) + L"x" + std::to_wstring(height));
    ScreenDC screen;
    if (!screen.handle) { Logger::Win32Error(L"GetDC(screen)", GetLastError()); return false; }
    MemoryDC memory(screen.handle);
    if (!memory.handle) { Logger::Win32Error(L"CreateCompatibleDC", GetLastError()); return false; }
    DibSelection dib(memory.handle, width, height);
    if (!dib.Valid()) { return false; }
    if (!BitBlt(memory.handle, 0, 0, width, height, screen.handle, first.x, first.y, SRCCOPY | CAPTUREBLT)) {
        Logger::Win32Error(L"BitBlt", GetLastError()); return false;
    }
    if (!GdiFlush()) { Logger::Win32Error(L"GdiFlush", GetLastError()); return false; }
    image.width = static_cast<UINT>(width);
    image.height = static_cast<UINT>(height);
    const auto* pixels = static_cast<const BYTE*>(dib.bits);
    image.pixels.assign(pixels, pixels + static_cast<size_t>(bytes));
    // GDI BI_RGB alpha is undefined. Make PNG pixels fully opaque.
    for (size_t i = 3; i < image.pixels.size(); i += 4) { image.pixels[i] = 255; }
    Logger::Info(L"BitBlt API succeeded; this alone does not mean Viewer content was captured");
    return true;
}
bool CaptureEngine::SavePng(const CapturedImage& image, const std::wstring& path) {
    if (!image.width || !image.height || image.pixels.size() != static_cast<size_t>(image.width) * image.height * 4 ||
        image.pixels.size() > std::numeric_limits<UINT>::max()) { Logger::Error(L"Invalid image for PNG encoder"); return false; }
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICStream> stream;
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    if (!Logger::CheckHR(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf())), L"CoCreateInstance(WIC)") ||
        !Logger::CheckHR(factory->CreateStream(stream.GetAddressOf()), L"WIC CreateStream") ||
        !Logger::CheckHR(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), L"WIC InitializeFromFilename") ||
        !Logger::CheckHR(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()), L"WIC CreateEncoder(PNG)") ||
        !Logger::CheckHR(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), L"WIC Encoder Initialize") ||
        !Logger::CheckHR(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr), L"WIC CreateNewFrame") ||
        !Logger::CheckHR(frame->Initialize(nullptr), L"WIC Frame Initialize") ||
        !Logger::CheckHR(frame->SetSize(image.width, image.height), L"WIC SetSize")) { return false; }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (!Logger::CheckHR(frame->SetPixelFormat(&format), L"WIC SetPixelFormat")) { return false; }
    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) { Logger::Error(L"PNG encoder did not accept BGRA"); return false; }
    if (!Logger::CheckHR(frame->WritePixels(image.height, image.width * 4, static_cast<UINT>(image.pixels.size()),
        const_cast<BYTE*>(image.pixels.data())), L"WIC WritePixels") ||
        !Logger::CheckHR(frame->Commit(), L"WIC Frame Commit") ||
        !Logger::CheckHR(encoder->Commit(), L"WIC Encoder Commit")) { return false; }
    Logger::Info(L"Saved PNG: " + path);
    return true;
}
bool CaptureEngine::HasViewerPattern(const CapturedImage& image) {
    if (image.pixels.size() != static_cast<size_t>(image.width) * image.height * 4) { return false; }
    const UINT sampleY = patternTop + patternCell / 2;
    if (image.height <= sampleY || image.width <= patternLeft + capturePattern.size() * patternCell) { return false; }
    for (size_t i = 0; i < capturePattern.size(); ++i) {
        const size_t x = patternLeft + i * patternCell + patternCell / 2;
        const size_t index = (static_cast<size_t>(sampleY) * image.width + x) * 4;
        const auto channelMatches = [](BYTE value, int expected) { return std::abs(static_cast<int>(value) - expected) <= 24; };
        if (!channelMatches(image.pixels[index], 255) || !channelMatches(image.pixels[index + 1], capturePattern[i] ? 0 : 255) ||
            !channelMatches(image.pixels[index + 2], capturePattern[i] ? 255 : 0)) { return false; }
    }
    return true;
}
bool CaptureEngine::IsNearlyBlack(const CapturedImage& image) {
    if (image.pixels.empty()) { return false; }
    size_t dark = 0;
    const size_t count = image.pixels.size() / 4;
    for (size_t i = 0; i < image.pixels.size(); i += 4) {
        if (image.pixels[i] <= 8 && image.pixels[i + 1] <= 8 && image.pixels[i + 2] <= 8) { ++dark; }
    }
    return static_cast<double>(dark) / static_cast<double>(count) >= 0.995;
}
}
