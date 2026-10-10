#pragma once
#include <array>
#include <cstdint>

namespace veil {
// Visible diagnostic pattern, not a protection mechanism or an authenticity proof.
inline constexpr std::array<bool, 16> capturePattern{true, false, true, true, false, false, true, false,
    false, true, false, true, false, true, true, false};
inline constexpr int patternLeft = 24;
inline constexpr int patternTop = 24;
inline constexpr int patternCell = 12;
}
