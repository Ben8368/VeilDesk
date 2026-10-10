#include "graphics/D3D11Renderer.h"
#include "common/Logger.h"
#include "common/CapturePattern.h"
#include <d2d1_1helper.h>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace veil {
D3D11Renderer::~D3D11Renderer() {
    ReleaseTargets();
    if (context_) { context_->ClearState(); context_->Flush(); }
    Logger::Info(L"Renderer released; presented frames=" + std::to_wstring(frame_));
}
bool D3D11Renderer::Initialize(HWND hwnd, bool tryDisplayOnly, bool simulateDisplayOnlyFailure) {
    hwnd_ = hwnd;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL actual{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, device_.GetAddressOf(), &actual, context_.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Warn(L"D3D11CreateDevice(hardware) unavailable; trying WARP software device: " + Logger::HrMessage(hr));
        device_.Reset(); context_.Reset();
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, device_.GetAddressOf(), &actual, context_.GetAddressOf());
    }
    if (!Logger::CheckHR(hr, L"D3D11CreateDevice")) { return false; }
    Logger::Info(L"D3D11 device created; feature level=" + std::to_wstring((static_cast<UINT>(actual) >> 12) & 0xF) +
        L"." + std::to_wstring((static_cast<UINT>(actual) >> 8) & 0xF));
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (!Logger::CheckHR(device_.As(&dxgiDevice), L"Query IDXGIDevice") ||
        !Logger::CheckHR(dxgiDevice->GetAdapter(adapter.GetAddressOf()), L"GetAdapter") ||
        !Logger::CheckHR(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())), L"GetParent(IDXGIFactory2)")) { return false; }
    DXGI_ADAPTER_DESC adapterDesc{};
    if (!Logger::CheckHR(adapter->GetDesc(&adapterDesc), L"IDXGIAdapter::GetDesc")) { return false; }
    Logger::Info(L"GPU / DXGI adapter name: " + std::wstring(adapterDesc.Description));
    RECT client{};
    if (!GetClientRect(hwnd_, &client)) { Logger::Win32Error(L"GetClientRect", GetLastError()); return false; }
    width_ = static_cast<UINT>(client.right - client.left);
    height_ = static_cast<UINT>(client.bottom - client.top);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_; desc.Height = height_;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (tryDisplayOnly) {
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY;
        // Explicit fault injection tests the fallback. It does not measure driver support.
        if (simulateDisplayOnlyFailure) { Logger::Warn(L"TEST: simulating DISPLAY_ONLY failure (DXGI_ERROR_UNSUPPORTED)"); }
        hr = simulateDisplayOnlyFailure ? DXGI_ERROR_UNSUPPORTED : factory->CreateSwapChainForHwnd(
            device_.Get(), hwnd_, &desc, nullptr, nullptr, swapChain_.GetAddressOf());
        if (SUCCEEDED(hr)) {
            swapFlags_ = desc.Flags;
            Logger::Info(L"DISPLAY_ONLY supported (swap chain creation accepted; capture behavior requires testing)");
        } else {
            Logger::Warn(L"DISPLAY_ONLY unavailable, fallback enabled; CreateSwapChainForHwnd: " + Logger::HrMessage(hr));
            swapChain_.Reset();
            desc.Flags = 0;
        }
    } else { Logger::Info(L"DISPLAY_ONLY disabled by diagnostic option"); }
    if (!swapChain_ && !Logger::CheckHR(factory->CreateSwapChainForHwnd(device_.Get(), hwnd_, &desc, nullptr, nullptr,
        swapChain_.GetAddressOf()), L"CreateSwapChainForHwnd(fallback)")) { return false; }
    Logger::Info(L"SwapChain created (HWND, flip sequential, BGRA, 2 buffers)");
    if (!Logger::CheckHR(factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER), L"MakeWindowAssociation")) { return false; }
    const D2D1_FACTORY_OPTIONS options{};
    if (!Logger::CheckHR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
        reinterpret_cast<void**>(textFactory_.GetAddressOf())), L"D2D1CreateFactory") ||
        !Logger::CheckHR(textFactory_->CreateDevice(dxgiDevice.Get(), textDevice_.GetAddressOf()), L"D2D CreateDevice") ||
        !Logger::CheckHR(textDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, textContext_.GetAddressOf()), L"D2D CreateDeviceContext") ||
        !Logger::CheckHR(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf())), L"DWriteCreateFactory")) { return false; }
    if (!Logger::CheckHR(writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 42.0F, L"en-US", titleFormat_.GetAddressOf()), L"CreateTextFormat(title)") ||
        !Logger::CheckHR(writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0F, L"en-US", bodyFormat_.GetAddressOf()), L"CreateTextFormat(body)") ||
        !Logger::CheckHR(textContext_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush_.GetAddressOf()), L"CreateSolidColorBrush")) { return false; }
    return CreateTargets();
}
void D3D11Renderer::ReleaseTargets() {
    if (textContext_) { textContext_->SetTarget(nullptr); }
    textTarget_.Reset();
    if (context_) { context_->OMSetRenderTargets(0, nullptr, nullptr); }
    renderTarget_.Reset();
}
bool D3D11Renderer::CreateTargets() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    if (!Logger::CheckHR(swapChain_->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf())), L"SwapChain::GetBuffer") ||
        !Logger::CheckHR(device_->CreateRenderTargetView(buffer.Get(), nullptr, renderTarget_.ReleaseAndGetAddressOf()), L"CreateRenderTargetView") ||
        !Logger::CheckHR(buffer.As(&surface), L"Query IDXGISurface")) { return false; }
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0F, 96.0F);
    if (!Logger::CheckHR(textContext_->CreateBitmapFromDxgiSurface(surface.Get(), &properties,
        textTarget_.ReleaseAndGetAddressOf()), L"CreateBitmapFromDxgiSurface")) { return false; }
    textContext_->SetTarget(textTarget_.Get());
    return true;
}
bool D3D11Renderer::Resize(UINT width, UINT height) {
    if (!swapChain_ || width == 0 || height == 0 || (width == width_ && height == height_)) { return true; }
    ReleaseTargets();
    context_->ClearState();
    HRESULT resize = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapFlags_);
    if (FAILED(resize) && DisplayOnlyEnabled() && resize != DXGI_ERROR_DEVICE_REMOVED && resize != DXGI_ERROR_DEVICE_RESET) {
        Logger::Warn(L"DISPLAY_ONLY resize failed; retrying without flag: " + Logger::HrMessage(resize));
        swapFlags_ = 0;
        resize = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapFlags_);
        if (SUCCEEDED(resize)) { Logger::Warn(L"DISPLAY_ONLY unavailable, fallback enabled after resize"); }
    }
    if (!Logger::CheckHR(resize, L"ResizeBuffers")) { return false; }
    width_ = width; height_ = height;
    if (!CreateTargets()) { return false; }
    Logger::Info(L"ResizeBuffers completed: " + std::to_wstring(width) + L"x" + std::to_wstring(height));
    return true;
}
bool D3D11Renderer::RenderFrame(UINT dpi) {
    const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_).count();
    const float color[]{0.08F + 0.04F * std::sin(seconds), 0.12F + 0.06F * std::sin(seconds * 0.7F),
        0.22F + 0.1F * std::sin(seconds * 0.5F), 1.0F};
    context_->ClearRenderTargetView(renderTarget_.Get(), color);
    textContext_->BeginDraw();
    textContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    textContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (size_t i = 0; i < capturePattern.size(); ++i) {
        brush_->SetColor(capturePattern[i] ? D2D1::ColorF(1.0F, 0.0F, 1.0F) : D2D1::ColorF(0.0F, 1.0F, 1.0F));
        const float left = static_cast<float>(patternLeft + static_cast<int>(i) * patternCell);
        textContext_->FillRectangle(D2D1::RectF(left, static_cast<float>(patternTop), left + patternCell,
            static_cast<float>(patternTop + patternCell)), brush_.Get());
    }
    textContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    brush_->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    const float scale = static_cast<float>(dpi) / 96.0F;
    textContext_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
    const float right = static_cast<float>(width_) / scale - 24.0F;
    const wchar_t title[] = L"SECURE CONTENT";
    textContext_->DrawText(title, static_cast<UINT32>(std::size(title) - 1), titleFormat_.Get(), D2D1::RectF(24, 64, right, 122), brush_.Get());
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::wostringstream body;
    body << L"Frame: " << frame_ << L"\nLocal time: " << std::setfill(L'0') << std::setw(4) << now.wYear << L"-"
        << std::setw(2) << now.wMonth << L"-" << std::setw(2) << now.wDay << L" "
        << std::setw(2) << now.wHour << L":" << std::setw(2) << now.wMinute << L":" << std::setw(2) << now.wSecond
        << L"\n\n" << protectionStatus_ << L"\nDISPLAY_ONLY: " << (DisplayOnlyEnabled() ? L"API enabled" : L"off / fallback")
        << L"\nF11: fullscreen / Esc: return\nWindows capture restrictions PoC";
    const std::wstring text = body.str();
    textContext_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), bodyFormat_.Get(),
        D2D1::RectF(24, 138, right, static_cast<float>(height_) / scale - 16.0F), brush_.Get());
    const HRESULT draw = textContext_->EndDraw();
    if (draw == D2DERR_RECREATE_TARGET) {
        Logger::Warn(L"D2D target lost; recreating targets");
        ReleaseTargets();
        return CreateTargets();
    }
    if (!Logger::CheckHR(draw, L"D2D EndDraw")) { return false; }
    const HRESULT present = swapChain_->Present(1, 0);
    if (present == DXGI_STATUS_OCCLUDED) {
        if (!occluded_) { Logger::Info(L"SwapChain occluded"); }
        occluded_ = true;
        return true;
    }
    occluded_ = false;
    if (FAILED(present) && DisplayOnlyEnabled() && present != DXGI_ERROR_DEVICE_REMOVED && present != DXGI_ERROR_DEVICE_RESET) {
        Logger::Warn(L"DISPLAY_ONLY Present failed; retrying without flag: " + Logger::HrMessage(present));
        ReleaseTargets();
        context_->ClearState();
        swapFlags_ = 0;
        if (!Logger::CheckHR(swapChain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0), L"ResizeBuffers(Present fallback)") ||
            !CreateTargets()) { return false; }
        Logger::Warn(L"DISPLAY_ONLY unavailable, fallback enabled after Present");
        return true;
    }
    if (!Logger::CheckHR(present, L"SwapChain::Present")) {
        if (present == DXGI_ERROR_DEVICE_REMOVED || present == DXGI_ERROR_DEVICE_RESET) {
            Logger::CheckHR(device_->GetDeviceRemovedReason(), L"Device removal reason");
        }
        return false;
    }
    ++frame_;
    if (frame_ == 1 || frame_ % 300 == 0) { Logger::Info(L"Presented frame " + std::to_wstring(frame_)); }
    return true;
}
}
