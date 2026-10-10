#include "broker/BenchmarkResources.h"
#include "broker/Handle.h"
#include "common/Logger.h"
#include <psapi.h>
#include <tlhelp32.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string_view>
#include <string>

namespace veil::broker {
namespace {
std::uint64_t Ticks(FILETIME time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
struct Counter {
    PDH_HCOUNTER value = nullptr;
    void Add(PDH_HQUERY query, const wchar_t* path) {
        const auto status = PdhAddEnglishCounterW(query, path, 0, &value);
        if (status != ERROR_SUCCESS) {
            value = nullptr;
            Logger::Warn(L"Benchmark GPU counter NOT AVAILABLE; PDH status=" + std::to_wstring(status));
        }
    }
    std::optional<double> Read(DWORD pid, bool maximum) const {
        if (!value) { return {}; }
        DWORD bytes = 0, count = 0;
        const auto status = PdhGetFormattedCounterArrayW(value, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, nullptr);
        if (status != PDH_MORE_DATA || !bytes || bytes > 16 * 1024 * 1024) { return {}; }
        // Allocate aligned storage; counter arrays contain pointers and doubles.
        std::vector<std::uint64_t> storage((bytes + 7) / 8);
        auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
        if (PdhGetFormattedCounterArrayW(value, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, items) != ERROR_SUCCESS) { return {}; }
        const auto prefix = L"pid_" + std::to_wstring(pid) + L"_";
        double result = 0;
        bool found = false;
        for (DWORD i = 0; i < count; ++i) {
            if (!items[i].szName || !std::wstring_view(items[i].szName).starts_with(prefix)) { continue; }
            if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) { return {}; }
            const auto number = items[i].FmtValue.doubleValue;
            if (!std::isfinite(number) || number < 0) { return {}; }
            result = maximum ? (std::max)(result, number) : result + number;
            found = true;
        }
        return found ? std::optional<double>(result) : std::nullopt;
    }
};
}
struct BenchmarkResources::Impl {
    PDH_HQUERY query = nullptr;
    Counter dedicated, shared, engines;
    std::array<std::uint64_t, 3> previousCpu{};
    std::optional<double> previousSeconds;
    Impl() {
        const auto status = PdhOpenQueryW(nullptr, 0, &query);
        if (status != ERROR_SUCCESS) {
            query = nullptr;
            Logger::Warn(L"Benchmark PDH NOT AVAILABLE; status=" + std::to_wstring(status));
            return;
        }
        dedicated.Add(query, L"\\GPU Process Memory(*)\\Dedicated Usage");
        shared.Add(query, L"\\GPU Process Memory(*)\\Shared Usage");
        engines.Add(query, L"\\GPU Engine(*)\\Utilization Percentage");
    }
    ~Impl() { if (query) { PdhCloseQuery(query); } }
};
BenchmarkResources::BenchmarkResources() : impl_(std::make_unique<Impl>()) {}
BenchmarkResources::~BenchmarkResources() = default;
bool BenchmarkResources::Sample(double seconds, const std::array<HANDLE, 3>& processes,
    std::array<ResourceSample, 3>& samples) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (!snapshot) { Logger::Win32Error(L"Benchmark thread snapshot", GetLastError()); return false; }
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    if (!Thread32First(snapshot.get(), &entry)) { Logger::Win32Error(L"Benchmark Thread32First", GetLastError()); return false; }
    std::array<DWORD, 3> pids{};
    for (size_t i = 0; i < processes.size(); ++i) {
        pids[i] = GetProcessId(processes[i]);
        if (!pids[i]) { Logger::Win32Error(L"Benchmark GetProcessId", GetLastError()); return false; }
    }
    std::array<DWORD, 3> threads{};
    do {
        for (size_t i = 0; i < pids.size(); ++i) { if (entry.th32OwnerProcessID == pids[i]) { ++threads[i]; } }
        entry.dwSize = sizeof(entry);
    } while (Thread32Next(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) { Logger::Win32Error(L"Benchmark Thread32Next", GetLastError()); return false; }
    const bool gpuAvailable = impl_->query && PdhCollectQueryData(impl_->query) == ERROR_SUCCESS;
    for (size_t i = 0; i < processes.size(); ++i) {
        auto& row = samples[i];
        row.seconds = seconds; row.role = static_cast<unsigned>(i); row.pid = pids[i]; row.threads = threads[i];
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        if (!GetProcessMemoryInfo(processes[i], reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) ||
            !GetProcessHandleCount(processes[i], &row.handles)) {
            Logger::Win32Error(L"Benchmark process resources", GetLastError()); return false;
        }
        row.privateBytes = memory.PrivateUsage; row.workingSet = memory.WorkingSetSize;
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(processes[i], &created, &exited, &kernel, &user)) {
            Logger::Win32Error(L"Benchmark GetProcessTimes", GetLastError()); return false;
        }
        const auto cpu = Ticks(kernel) + Ticks(user);
        if (impl_->previousSeconds && seconds > *impl_->previousSeconds && cpu >= impl_->previousCpu[i]) {
            // 100% = one logical processor; deliberately not divided by machine CPU count.
            row.cpuPercent = static_cast<double>(cpu - impl_->previousCpu[i]) / 100000.0 / (seconds - *impl_->previousSeconds);
        }
        impl_->previousCpu[i] = cpu;
        if (gpuAvailable) {
            row.gpuDedicated = impl_->dedicated.Read(pids[i], false);
            row.gpuShared = impl_->shared.Read(pids[i], false);
            row.gpuEngineMax = impl_->engines.Read(pids[i], true);
        }
    }
    impl_->previousSeconds = seconds;
    return true;
}
}
