#pragma once
#include "broker/BenchmarkResources.h"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <vector>

namespace veil::broker {
struct BenchmarkConfig {
    unsigned durationSeconds = 60, pixelWidth = 1920, pixelHeight = 1080, fps = 60, sampleSeconds = 5;
    bool Valid() const noexcept;
    unsigned Slots() const noexcept { return durationSeconds * fps; }
};
class Benchmark final {
public:
    using Clock = std::chrono::steady_clock;
    Benchmark(const BenchmarkConfig& config, const std::filesystem::path& directory);
    ~Benchmark();
    bool Pace(unsigned slot) const;
    void Produced() noexcept { ++produced_; }
    void Delivered(Clock::time_point started);
    enum class Stage : unsigned { Produce, Inspect, View, Count };
    void Record(Stage stage, Clock::time_point started);
    bool Sample(HANDLE producer, HANDLE viewer, bool force = false);
    bool Save(const std::wstring& adapter) const;
    unsigned ElapsedSlots() const;
private:
    EXECUTION_STATE previousExecutionState_ = 0;
    BenchmarkConfig config_;
    std::filesystem::path directory_;
    Clock::time_point started_;
    BenchmarkResources sampler_;
    std::ofstream samples_;
    unsigned produced_ = 0;
    std::vector<double> latencies_;
    std::vector<ResourceSample> resources_;
    double stageMs_[static_cast<unsigned>(Stage::Count)]{};
};
}
