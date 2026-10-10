#pragma once
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <filesystem>

namespace veil {
// Diagnostic only: one UI writer, one observer. Never reads pixels or calls D3D.
class ProgressWatchdog final {
public:
    explicit ProgressWatchdog(const std::filesystem::path& log);
    ~ProgressWatchdog();
    // Phase labels must have static storage (all callers use string literals).
    void Checkpoint(const char* phase, std::uint64_t frame = UINT64_MAX, unsigned message = 0) noexcept;
private:
    void Observe(std::stop_token stop);
    std::atomic<std::uint64_t> sequence_{0}, changed_{0}, frame_{0};
    std::atomic<unsigned> message_{0};
    std::atomic<const char*> phase_{"startup"};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::ofstream log_;
    std::jthread thread_;
};
}
