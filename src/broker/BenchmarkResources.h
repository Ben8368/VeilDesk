#pragma once
#include <Windows.h>
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace veil::broker {
struct ResourceSample {
    double seconds = 0;
    unsigned displayed = 0;
    unsigned produced = 0;
    unsigned dropped = 0;
    unsigned role = 0;
    DWORD pid = 0;
    SIZE_T privateBytes = 0, workingSet = 0;
    DWORD handles = 0, threads = 0;
    std::optional<double> cpuPercent, gpuDedicated, gpuShared, gpuEngineMax;
};
// Retained process handles; PDH GPU counters are optional, never interpreted as zero on failure.
class BenchmarkResources final {
public:
    BenchmarkResources();
    ~BenchmarkResources();
    BenchmarkResources(const BenchmarkResources&) = delete;
    BenchmarkResources& operator=(const BenchmarkResources&) = delete;
    bool Sample(double seconds, const std::array<HANDLE, 3>& processes,
        std::array<ResourceSample, 3>& samples);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
