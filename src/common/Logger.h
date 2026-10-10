#pragma once
#include <Windows.h>
#include <string>
#include <string_view>

namespace veil {
class Logger final {
public:
    static void Initialize(const std::wstring& file, bool externalSinks = true);
    static void SetExternalSinks(bool enabled);
    static void Info(std::wstring_view message);
    static void Warn(std::wstring_view message);
    static void Error(std::wstring_view message);
    static void Win32Error(std::wstring_view operation, DWORD error);
    static bool CheckHR(HRESULT result, std::wstring_view operation);
    static std::wstring HrMessage(HRESULT result);
    static std::wstring SystemMessage(DWORD code);
private:
    static void Write(std::wstring_view level, std::wstring_view message);
};
}
