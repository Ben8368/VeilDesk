#pragma once
#include "broker/Handle.h"
#include "broker/Protocol.h"
#include <string>

namespace veil::broker {
bool Receive(HANDLE pipe, Message& message, DWORD timeoutMs);
bool Send(HANDLE pipe, const Message& message);
struct Child {
    Handle process, input, output;
    DWORD pid = 0;
    bool Start(const std::wstring& executable, const std::wstring& options);
    bool Finish();
    ~Child();
};
}
