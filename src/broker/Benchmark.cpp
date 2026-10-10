#include "broker/Benchmark.h"
#include "common/PreciseTimer.h"
#include "common/Logger.h"
#include <wtsapi32.h>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iomanip>

namespace veil::broker {
namespace {
bool ActiveUnlockedSession() {
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        Logger::Win32Error(L"Benchmark ProcessIdToSessionId", GetLastError()); return false;
    }
    wchar_t* buffer = nullptr; DWORD bytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSSessionInfoEx, &buffer, &bytes)) {
        Logger::Win32Error(L"Benchmark WTSInfoEx; session state NOT AVAILABLE", GetLastError()); return false;
    }
    bool active = false;
    if (bytes >= sizeof(WTSINFOEXW)) {
        const auto info = reinterpret_cast<const WTSINFOEXW*>(buffer);
        active = info->Level == 1 && info->Data.WTSInfoExLevel1.SessionId == session &&
            info->Data.WTSInfoExLevel1.SessionState == WTSActive && info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_UNLOCK;
    }
    WTSFreeMemory(buffer);
    if (!active) { Logger::Error(L"Benchmark requires active unlocked desktop; unknown/locked/disconnected state rejects run"); }
    return active;
}
}

bool BenchmarkConfig::Valid() const noexcept {
    return durationSeconds >= 1 && durationSeconds <= 7200 && fps >= 1 && fps <= 240 &&
        pixelWidth >= 216 && pixelWidth <= 4096 && pixelHeight >= 36 && pixelHeight <= 4096 &&
        sampleSeconds >= 1 && sampleSeconds <= 60;
}
Benchmark::Benchmark(const BenchmarkConfig& config, const std::filesystem::path& directory)
    : config_(config), directory_(directory), started_(Clock::now()), samples_(directory / "resources.csv") {
    if (!ActiveUnlockedSession()) { throw std::runtime_error("benchmark desktop unavailable"); }
    latencies_.reserve(config.Slots());
    resources_.reserve((config.durationSeconds / config.sampleSeconds + 3) * 3);
    samples_ << "seconds,role,pid,produced_frames,displayed_frames,dropped_slots,private_bytes,handles,working_set_bytes,threads,cpu_percent_one_core,gpu_dedicated_bytes,gpu_shared_bytes,gpu_engine_max_percent\n";
    samples_ << std::setprecision(12);
    // Temporary thread request only; does not change power/security policy or bypass a user lock.
    previousExecutionState_ = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    if (!previousExecutionState_) { Logger::Error(L"Benchmark SetThreadExecutionState failed"); throw std::runtime_error("display activity request failed"); }
}
Benchmark::~Benchmark() {
    if (previousExecutionState_ && !SetThreadExecutionState(previousExecutionState_)) {
        Logger::Warn(L"Benchmark restoring previous execution state failed");
    }
}
unsigned Benchmark::ElapsedSlots() const {
    return static_cast<unsigned>(std::chrono::duration<double>(Clock::now() - started_).count() * config_.fps);
}
bool Benchmark::Pace(unsigned slot) const {
    const auto target = started_ + std::chrono::nanoseconds(static_cast<long long>(slot) * 1000000000LL / config_.fps);
    thread_local PreciseTimer pacing;
    while (Clock::now() < target) { pacing.Wait(1); }
    return Clock::now() < started_ + std::chrono::seconds(config_.durationSeconds);
}
void Benchmark::Delivered(Clock::time_point started) {
    latencies_.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
}
void Benchmark::Record(Stage stage, Clock::time_point started) {
    stageMs_[static_cast<unsigned>(stage)] += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}
bool Benchmark::Sample(HANDLE producer, HANDLE viewer, bool force) {
    const auto sampleStarted = Clock::now();
    const double seconds = std::chrono::duration<double>(sampleStarted - started_).count();
    if (!force && !resources_.empty() && seconds - resources_.back().seconds < config_.sampleSeconds) { return true; }
    if (!ActiveUnlockedSession()) { return false; }
    std::array<ResourceSample, 3> rows{};
    if (!sampler_.Sample(seconds, {GetCurrentProcess(), producer, viewer}, rows)) { return false; }
    const char* roles[]{"Broker", "Producer", "Viewer"};
    const auto displayed = static_cast<unsigned>(latencies_.size());
    const auto elapsedSlots = (std::min)(ElapsedSlots(), config_.Slots());
    for (auto& row : rows) {
        row.produced = produced_; row.displayed = displayed;
        row.dropped = elapsedSlots > displayed ? elapsedSlots - displayed : 0;
        samples_ << row.seconds << ',' << roles[row.role] << ',' << row.pid << ',' << row.produced << ',' << row.displayed
            << ',' << row.dropped << ',' << row.privateBytes << ',' << row.handles << ',' << row.workingSet << ',' << row.threads;
        for (auto value : {row.cpuPercent, row.gpuDedicated, row.gpuShared, row.gpuEngineMax}) {
            samples_ << ',';
            if (value) { samples_ << *value; } else { samples_ << "NOT AVAILABLE"; }
        }
        samples_ << '\n';
        resources_.push_back(row);
    }
    samples_.flush();
    const auto sampleMs = std::chrono::duration<double, std::milli>(Clock::now() - sampleStarted).count();
    if (sampleMs >= 100) { Logger::Warn(L"Benchmark resource sample stalled; duration_ms=" + std::to_wstring(sampleMs)); }
    return samples_.good();
}
bool Benchmark::Save(const std::wstring& adapter) const {
    if (latencies_.empty() || resources_.empty()) { return false; }
    auto ordered = latencies_; std::sort(ordered.begin(), ordered.end());
    const double mean = std::accumulate(ordered.begin(), ordered.end(), 0.0) / ordered.size();
    const auto quantile = [&ordered](double fraction) { return ordered[static_cast<size_t>(std::ceil(fraction * ordered.size())) - 1]; };
    std::string adapterAscii;
    for (wchar_t character : adapter) { if (character > 127) { return false; } adapterAscii.push_back(static_cast<char>(character)); }
    const auto aggregate = [this](size_t first, bool handles) {
        std::uint64_t value = 0;
        for (size_t i = first; i < first + 3; ++i) { value += handles ? resources_[i].handles : resources_[i].privateBytes; }
        return value;
    };
    std::ofstream summary(directory_ / "benchmark.json");
    summary << std::setprecision(12) << "{\n  \"adapter_luid\":\"" << adapterAscii << "\",\n  \"width\":" << config_.pixelWidth
        << ", \"height\":" << config_.pixelHeight << ", \"target_fps\":" << config_.fps
        << ",\n  \"requested_duration_seconds\":" << config_.durationSeconds
        << ", \"duration_seconds\":" << std::chrono::duration<double>(Clock::now() - started_).count()
        << ", \"sample_interval_seconds\":" << config_.sampleSeconds
        << ",\n  \"target_slots\":" << config_.Slots() << ", \"frames_produced\":" << produced_
        << ", \"frames_displayed\":" << ordered.size() << ", \"frames_dropped\":" << config_.Slots() - ordered.size()
        << ",\n  \"average_delivery_ms\":" << mean << ", \"p95_delivery_ms\":" << quantile(0.95) << ", \"p99_delivery_ms\":" << quantile(0.99)
        << ",\n  \"average_produce_ms\":" << stageMs_[0] / ordered.size() << ", \"average_inspect_ms\":" << stageMs_[1] / ordered.size()
        << ", \"average_view_ms\":" << stageMs_[2] / ordered.size()
        << ",\n  \"private_bytes_first\":" << aggregate(0, false) << ", \"private_bytes_last\":" << aggregate(resources_.size() - 3, false)
        << ", \"handles_first\":" << aggregate(0, true) << ", \"handles_last\":" << aggregate(resources_.size() - 3, true)
        << ",\n  \"cpu_semantics\":\"100 percent = one logical processor\","
        << "\n  \"gpu_semantics\":\"PDH per-PID sum of memory instances; busiest engine utilization; missing/invalid = NOT AVAILABLE. WDDM accounting, not unique physical allocation ownership.\","
        << "\n  \"latency_semantics\":\"request through Present/ACK, not physical scanout\"\n}\n";
    summary.flush();
    return summary.good();
}
}
