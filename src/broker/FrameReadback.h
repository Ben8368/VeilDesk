#pragma once
#include "broker/SharedFrame.h"
#include <vector>

namespace veil::broker {
// Explicit recorder/test-only CPU boundary; never linked into SecureViewer.
std::vector<BYTE> FramePixels(std::uint64_t frame, UINT pixelWidth = width, UINT pixelHeight = height);
class FrameReadback final {
public:
    bool Read(ID3D11Texture2D* source, std::vector<BYTE>& pixels, ZeroCopyStats* stats = nullptr);
private:
    ComPtr<ID3D11Texture2D> staging_;
};
}
