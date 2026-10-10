#include "common/ProgressWatchdog.h"
#include <Windows.h>
#include <chrono>
#include <stdexcept>

namespace veil {
ProgressWatchdog::ProgressWatchdog(const std::filesystem::path& log) : log_(log) {
    if (!log_) { throw std::runtime_error("progress log unavailable"); }
    changed_.store(GetTickCount64());
    thread_ = std::jthread([this](std::stop_token stop) { Observe(stop); });
}
ProgressWatchdog::~ProgressWatchdog() { thread_.request_stop(); wake_.notify_all(); }
void ProgressWatchdog::Checkpoint(const char* phase, std::uint64_t frame) noexcept {
    sequence_.fetch_add(1);
    phase_.store(phase);
    if (frame != UINT64_MAX) { frame_.store(frame); }
    changed_.store(GetTickCount64());
    sequence_.fetch_add(1);
}
void ProgressWatchdog::Observe(std::stop_token stop) {
    std::uint64_t reported = UINT64_MAX;
    unsigned events = 0;
    while (!stop.stop_requested()) {
        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(250), [&] { return stop.stop_requested(); });
        lock.unlock();
        if (stop.stop_requested()) { break; }
        const auto sequence = sequence_.load();
        if (sequence & 1) { continue; }
        const auto phase = phase_.load(); const auto frame = frame_.load(); const auto changed = changed_.load();
        if (sequence != sequence_.load()) { continue; }
        const auto now = GetTickCount64();
        if (now >= changed && now - changed >= 1000 && sequence != reported) {
            // Independent file sink: observing a blocked Logger must not wait on Logger's mutex.
            if (events < 128) {
                log_ << "tick_ms=" << now << ",frame=" << frame << ",phase=" << phase << ",inactive_ms=" << now - changed << '\n';
                log_.flush(); ++events;
                if (events == 128) { log_ << "EVENT CAP REACHED; further events NOT RECORDED\n" << std::flush; }
            }
            reported = sequence;
        }
    }
}
}
