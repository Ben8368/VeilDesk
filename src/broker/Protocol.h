#pragma once
#include <cstdint>

namespace veil::broker {
inline constexpr std::uint32_t magic = 0x5645494c;
inline constexpr std::uint32_t version = 2;
inline constexpr std::uint32_t width = 320, height = 180;
enum class Kind : std::uint32_t { Frame = 1, Ack = 2, Stop = 3, Denied = 4 };
enum class Status : std::uint32_t { Ok, Invalid, Expired, Revoked, Replay, Timeout, Failed };
// Same-host x64 protocol; fixed fields, no pointers, strings or variable payloads.
struct Message {
    std::uint32_t signature = magic;
    std::uint32_t protocol = version;
    Kind kind = Kind::Frame;
    Status status = Status::Ok;
    std::uint64_t generation = 1;
    std::uint64_t frame = 0;
    std::uint64_t handle = 0;
    std::uint64_t deadline = 0; // GetTickCount64, machine boot relative
    std::uint32_t pixelWidth = width;
    std::uint32_t pixelHeight = height;
    std::uint32_t adapterLow = 0;
    std::int32_t adapterHigh = 0;
};
static_assert(sizeof(Message) == 64);
struct Lease {
    std::uint64_t generation = 1, deadline = 0, lastFrame = 0;
    bool granted = false, revoked = false;
    Status Check(const Message& message, std::uint64_t now) const noexcept {
        if (!granted || message.signature != magic || message.protocol != version ||
            message.kind != Kind::Frame || message.status != Status::Ok ||
            message.generation != generation || message.pixelWidth != width || message.pixelHeight != height) {
            return Status::Invalid;
        }
        if (revoked) { return Status::Revoked; }
        if (now >= deadline || message.deadline != deadline) { return Status::Expired; }
        if (message.frame != lastFrame + 1) { return Status::Replay; }
        return Status::Ok;
    }
};
}
