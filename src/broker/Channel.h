#pragma once
#include "broker/Handle.h"
#include "broker/Protocol.h"
#include "broker/ProcessIdentity.h"
#include <string>

namespace veil::broker {
bool Receive(HANDLE pipe, Message& message, DWORD timeoutMs, const wchar_t* context = L"unspecified");
bool Send(HANDLE pipe, const Message& message);
struct Child {
    Handle process, input, output;
    ProcessIdentity identity;
    ChildRole role = ChildRole::None;
    bool enrolled = false;
    bool Start(const std::wstring& executable, const std::wstring& options, ChildRole approvedRole);
    bool Finish();
    ~Child();
};
}
