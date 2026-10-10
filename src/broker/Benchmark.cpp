#include "broker/Benchmark.h"
#include "common/Logger.h"
#include "common/PreciseTimer.h"
#include <psapi.h>
#include <algorithm>
#include <fstream>
#include <numeric>
#include <cmath>

namespace veil::broker {
Benchmark::Benchmark() : started_(Clock::now()) { latencies_.reserve(3600); resources_.reserve(65); }
unsigned Benchmark::ElapsedSlots() const {
    return static_cast<unsigned>(std::chrono::duration<double>(Clock::now() - started_).count() * 60);
}
bool Benchmark::Pace(unsigned slot) const {
    const auto target = started_ + std::chrono::nanoseconds(static_cast<long long>(slot) * 1000000000LL / 60);
    thread_local PreciseTimer pacing;
    while (Clock::now() < target) { pacing.Wait(1); }
    return Clock::now() < started_ + std::chrono::seconds(60);
}
void Benchmark::Delivered(Clock::time_point started) {
    latencies_.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
}
void Benchmark::Record(Stage stage, Clock::time_point started) {
    stageMs_[static_cast<unsigned>(stage)] += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}
bool Benchmark::Sample(HANDLE producer, HANDLE viewer) {
    const double seconds = std::chrono::duration<double>(Clock::now() - started_).count();
    if (!resources_.empty() && seconds - resources_.back().seconds < 1.0) { return true; }
    SIZE_T bytes = 0; DWORD handles = 0;
    for (HANDLE process : {GetCurrentProcess(), producer, viewer}) {
        PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
        DWORD count = 0;
        if (!GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
            Logger::Win32Error(L"Benchmark GetProcessMemoryInfo", GetLastError()); return false;
        }
        if (!GetProcessHandleCount(process, &count)) {
            Logger::Win32Error(L"Benchmark GetProcessHandleCount", GetLastError()); return false;
        }
        bytes += counters.PrivateUsage; handles += count;
    }
    resources_.push_back({std::chrono::duration<double>(Clock::now() - started_).count(), bytes, handles});
    return true;
}
bool Benchmark::Save(const std::filesystem::path& directory, unsigned produced, const std::wstring& adapter) const {
    if (latencies_.empty() || resources_.empty()) { return false; }
    auto ordered = latencies_; std::sort(ordered.begin(), ordered.end());
    const double mean = std::accumulate(ordered.begin(), ordered.end(), 0.0) / ordered.size();
    const double p95 = ordered[static_cast<size_t>(std::ceil(0.95 * ordered.size())) - 1];
    std::ofstream samples(directory / "resources.csv");
    samples << "seconds,aggregate_private_bytes,aggregate_handles\n";
    for (const auto& sample : resources_) { samples << sample.seconds << ',' << sample.privateBytes << ',' << sample.handles << '\n'; }
    // Post-warmup trend is descriptive; GPU allocations are not represented by private bytes.
    double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
    for (const auto& sample : resources_) {
        if (sample.seconds < 10) { continue; }
        sx += sample.seconds; sy += static_cast<double>(sample.privateBytes);
        sxx += sample.seconds * sample.seconds; sxy += sample.seconds * static_cast<double>(sample.privateBytes); ++n;
    }
    const double divisor = n * sxx - sx * sx;
    const double slope = divisor > 0 ? (n * sxy - sx * sy) / divisor : 0;
    std::string adapterAscii;
    for (wchar_t character : adapter) {
        if (character > 127) { return false; }
        adapterAscii.push_back(static_cast<char>(character));
    }
    std::ofstream summary(directory / "benchmark.json");
    summary << "{\n  \"adapter_luid\":\"" << adapterAscii << "\",\n  \"width\":1920, \"height\":1080, \"target_fps\":60,\n  \"duration_seconds\":"
        << std::chrono::duration<double>(Clock::now() - started_).count()
        << ",\n  \"frames_produced\":" << produced << ", \"frames_displayed\":" << ordered.size()
        << ", \"frames_dropped\":" << 3600 - ordered.size()
        << ",\n  \"average_delivery_ms\":" << mean << ", \"p95_delivery_ms\":" << p95
        << ",\n  \"average_produce_ms\":" << stageMs_[0] / ordered.size() << ", \"average_inspect_ms\":" << stageMs_[1] / ordered.size()
        << ", \"average_view_ms\":" << stageMs_[2] / ordered.size()
        << ",\n  \"private_bytes_first\":" << resources_.front().privateBytes
        << ", \"private_bytes_last\":" << resources_.back().privateBytes
        << ",\n  \"post_warmup_bytes_per_second\":" << slope
        << ",\n  \"handles_first\":" << resources_.front().handles << ", \"handles_last\":" << resources_.back().handles
        << "\n}\n";
    samples.flush(); summary.flush();
    return samples.good() && summary.good();
}
}
