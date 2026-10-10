#pragma once
#include <Windows.h>
#include <filesystem>
#include <chrono>
#include <vector>

namespace veil::broker {
class Benchmark final {
public:
    using Clock = std::chrono::steady_clock;
    Benchmark();
    bool Pace(unsigned slot) const;
    void Delivered(Clock::time_point started);
    bool Sample(HANDLE producer, HANDLE viewer);
    bool Save(const std::filesystem::path& directory, unsigned produced, const std::wstring& adapter) const;
    unsigned ElapsedSlots() const;
private:
    struct Resources { double seconds; SIZE_T privateBytes; DWORD handles; };
    Clock::time_point started_;
    std::vector<double> latencies_;
    std::vector<Resources> resources_;
};
}
