#pragma once
#include <Windows.h>
#include <string>
#include <vector>

namespace veil {
struct CapturedImage {
    UINT width = 0;
    UINT height = 0;
    std::vector<BYTE> pixels;
};
class CaptureEngine final {
public:
    // Ordinary screen BitBlt over a selected visible client rectangle. No hooks,
    // injection, PrintWindow fallback or affinity changes in the target process.
    static bool CaptureClient(HWND hwnd, CapturedImage& image);
    static bool SavePng(const CapturedImage& image, const std::wstring& path);
    static bool HasViewerPattern(const CapturedImage& image);
    static bool IsNearlyBlack(const CapturedImage& image);
};
}
