#include "broker/Channel.h"
#include "common/Logger.h"
#include <vector>

namespace veil::broker {
bool Receive(HANDLE pipe, Message& message, DWORD timeoutMs) {
    const auto end = GetTickCount64() + timeoutMs;
    DWORD available = 0;
    while (GetTickCount64() < end) {
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            Logger::Win32Error(L"PeekNamedPipe", GetLastError()); return false;
        }
        if (available >= sizeof(Message)) {
            DWORD read = 0;
            if (!ReadFile(pipe, &message, sizeof(message), &read, nullptr)) {
                Logger::Win32Error(L"ReadFile(protocol)", GetLastError()); return false;
            }
            if (read != sizeof(message) || message.signature != magic || message.protocol != version) {
                Logger::Error(L"Invalid protocol header/size/version"); return false;
            }
            return true;
        }
        Sleep(2);
    }
    Logger::Error(L"Protocol deadline exceeded"); return false;
}
bool Send(HANDLE pipe, const Message& message) {
    // One small outstanding request per pipe; ACK required before the next write.
    DWORD written = 0;
    if (!WriteFile(pipe, &message, sizeof(message), &written, nullptr)) {
        Logger::Win32Error(L"WriteFile(protocol)", GetLastError()); return false;
    }
    if (written != sizeof(message)) { Logger::Error(L"Short protocol write"); return false; }
    return true;
}
bool Child::Start(const std::wstring& executable, const std::wstring& options) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE childRead = nullptr, parentWrite = nullptr, parentRead = nullptr, childWrite = nullptr;
    if (!CreatePipe(&childRead, &parentWrite, &security, 4096)) {
        Logger::Win32Error(L"CreatePipe(input)", GetLastError()); return false;
    }
    Handle read(childRead); input.reset(parentWrite);
    if (!CreatePipe(&parentRead, &childWrite, &security, 4096)) {
        Logger::Win32Error(L"CreatePipe(output)", GetLastError()); return false;
    }
    Handle write(childWrite); output.reset(parentRead);
    if (!SetHandleInformation(input.get(), HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(output.get(), HANDLE_FLAG_INHERIT, 0)) {
        Logger::Win32Error(L"SetHandleInformation", GetLastError()); return false;
    }
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    if (bytes == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        Logger::Win32Error(L"InitializeProcThreadAttributeList(size)", GetLastError()); return false;
    }
    std::vector<unsigned char> storage(bytes);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) {
        Logger::Win32Error(L"InitializeProcThreadAttributeList", GetLastError()); return false;
    }
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Cleanup() { DeleteProcThreadAttributeList(value); } } cleanup{attributes};
    HANDLE list[]{read.get(), write.get()};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, list, sizeof(list), nullptr, nullptr)) {
        Logger::Win32Error(L"UpdateProcThreadAttribute(handle list)", GetLastError()); return false;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.lpAttributeList = attributes;
    auto command = L"\"" + executable + L"\" " + options + L" --channel " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(read.get())) + L" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(write.get()));
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &info)) {
        Logger::Win32Error(L"CreateProcessW(approved child)", GetLastError()); return false;
    }
    process.reset(info.hProcess); Handle thread(info.hThread); pid = info.dwProcessId;
    Logger::Info(L"Approved child retained as process object; no PID/path-only attachment");
    return true;
}
bool Child::Finish() {
    input.reset(); output.reset();
    const DWORD wait = WaitForSingleObject(process.get(), 5000);
    if (wait != WAIT_OBJECT_0) {
        if (wait == WAIT_FAILED) { Logger::Win32Error(L"WaitForSingleObject(child)", GetLastError()); }
        else { Logger::Error(L"Child shutdown timeout"); }
        return false;
    }
    DWORD code = 1;
    if (!GetExitCodeProcess(process.get(), &code)) { Logger::Win32Error(L"GetExitCodeProcess", GetLastError()); return false; }
    process.reset();
    if (code != 0) { Logger::Error(L"Child failed, exit=" + std::to_wstring(code)); return false; }
    return true;
}
Child::~Child() {
    input.reset(); output.reset();
    if (process && WaitForSingleObject(process.get(), 5000) == WAIT_TIMEOUT) {
        // Only the exact child created by this object, never an attached process.
        if (!TerminateProcess(process.get(), 1)) { Logger::Win32Error(L"TerminateProcess(owned child)", GetLastError()); }
        else { WaitForSingleObject(process.get(), 5000); }
    }
}
}
