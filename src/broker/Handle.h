#pragma once
#include <Windows.h>
#include <utility>

namespace veil::broker {
class Handle final {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept { reset(other.release()); return *this; }
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE release() noexcept { return std::exchange(value_, nullptr); }
    void reset(HANDLE value = nullptr) noexcept {
        if (*this) { CloseHandle(value_); }
        value_ = value;
    }
private:
    HANDLE value_;
};
}
