#include "common/Logger.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <vector>

namespace {
std::mutex logMutex;
std::ofstream logFile;
}
namespace veil {
void Logger::Initialize(const std::wstring& file) {
    std::scoped_lock lock(logMutex);
    logFile.open(std::filesystem::path(file), std::ios::trunc);
    if (!logFile) {
        OutputDebugStringW(L"[ERROR] Cannot open log file\n");
        std::wcerr << L"[ERROR] Cannot open log file: " << file << L'\n';
    }
}
void Logger::Write(std::wstring_view level, std::wstring_view message) {
    std::scoped_lock lock(logMutex);
    const std::wstring line = L"[" + std::wstring(level) + L"] " + std::wstring(message) + L"\n";
    OutputDebugStringW(line.c_str());
    std::wcout << line << std::flush;
    const int count = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    if (count > 0 && logFile) {
        std::string utf8(static_cast<size_t>(count), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8.data(), count, nullptr, nullptr) > 0) {
            logFile << utf8 << std::flush;
        }
    }
}
void Logger::Info(std::wstring_view message) { Write(L"INFO", message); }
void Logger::Warn(std::wstring_view message) { Write(L"WARN", message); }
void Logger::Error(std::wstring_view message) { Write(L"ERROR", message); }
std::wstring Logger::SystemMessage(DWORD code) {
    std::vector<wchar_t> buffer(2048);
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr);
    return length ? std::wstring(buffer.data(), length) : L"No system message available";
}
void Logger::Win32Error(std::wstring_view operation, DWORD error) {
    Error(std::wstring(operation) + L" failed: " + std::to_wstring(error) + L" - " + SystemMessage(error));
}
bool Logger::CheckHR(HRESULT result, std::wstring_view operation) {
    if (SUCCEEDED(result)) { return true; }
    Error(std::wstring(operation) + L" failed: " + HrMessage(result));
    return false;
}
std::wstring Logger::HrMessage(HRESULT result) {
    std::wostringstream code;
    code << L"0x" << std::hex << std::uppercase << static_cast<unsigned long>(result);
    return code.str() + L" - " + SystemMessage(static_cast<DWORD>(result));
}
}
