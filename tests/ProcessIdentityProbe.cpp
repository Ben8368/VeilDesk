#include "broker/Channel.h"
#include "common/Logger.h"
#include <iostream>

using namespace veil;
using namespace veil::broker;
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 5 && std::wstring(argv[1]) == L"--identity-child" && std::wstring(argv[2]) == L"--channel") {
            Handle input(reinterpret_cast<HANDLE>(std::stoull(argv[3]))), output(reinterpret_cast<HANDLE>(std::stoull(argv[4])));
            Message stop;
            return Receive(input.get(), stop, 5000) && stop.kind == Kind::Stop ? 0 : 1;
        }
        Logger::Initialize(L"ProcessIdentityProbe.log");
        ProcessIdentity current;
        if (!ReadProcessIdentity(GetCurrentProcess(), current) || current.userSid.empty() || !current.creationTime ||
            !MatchesRetainedProcess(GetCurrentProcess(), current)) { return 1; }
        for (unsigned field = 0; field < 5; ++field) {
            auto forged = current;
            if (field == 0) { ++forged.pid; }
            if (field == 1) { ++forged.creationTime; }
            if (field == 2) { ++forged.sessionId; }
            if (field == 3) { forged.userSid += L"-0"; }
            if (field == 4) { forged.imagePath += L".invalid"; }
            if (MatchesRetainedProcess(GetCurrentProcess(), forged)) { return 1; }
        }
        Child rejected;
        if (rejected.Start(current.imagePath, L"--identity-child", ChildRole::None) ||
            rejected.Start(current.imagePath + L".invalid", L"--identity-child", ChildRole::Producer)) { return 1; }
        Child child;
        if (!child.Start(current.imagePath, L"--identity-child", ChildRole::Producer) || !child.enrolled ||
            !SamePrincipal(current, child.identity) || child.identity.pid == current.pid ||
            !MatchesRetainedProcess(child.process.get(), child.identity)) { return 1; }
        HANDLE raw = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), child.process.get(), GetCurrentProcess(), &raw, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            Logger::Win32Error(L"Identity probe retained duplicate", GetLastError()); return 1;
        }
        Handle retained(raw); auto identity = child.identity;
        Message stop; stop.kind = Kind::Stop;
        if (!Send(child.input.get(), stop) || !child.Finish() || MatchesRetainedProcess(retained.get(), identity)) { return 1; }
        std::uint64_t generation = 0;
        if (!CreateSessionGeneration(generation) || !generation || generation == (std::numeric_limits<std::uint64_t>::max)()) { return 1; }
        std::wcout << L"ProcessIdentityProbe passed: real process/token identity, five forged snapshot rejections, own-tool enrollment before resume, exit denial, random session generation. Not a real PID reuse or publisher verification test.\n";
        return 0;
    } catch (...) { return 1; }
}
