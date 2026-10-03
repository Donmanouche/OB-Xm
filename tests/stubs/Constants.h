// Test-only stand-in for OB-Xf src/core/Constants.h (constants come from obxf_shim.h).
#pragma once
#include <array>
#include <string>
#include <iostream>
#include "configuration.h"
#include "juce_stub.h"

static constexpr int syncedRatesCount{21};
constexpr std::array<float, syncedRatesCount> syncedRates{
    1.f / 12.f, 1.f / 8.f, 1.f / 6.f, 3.f / 16.f, 1.f / 4.f, 1.f / 3.f, 3.f / 8.f,
    1.f / 2.f,  2.f / 3.f, 3.f / 4.f, 1.f,        3.f / 2.f, 4.f / 3.f, 2.f,
    8.f / 3.f,  3.f,       4.f,       6.f,        8.f,       12.f,      16.f};

// Remaining constants of the real Constants.h (dc, ln2, mult, pi come from obxf_shim.h)
constexpr float twoPi = 2.f * pi;
constexpr float invPi = 1.f / pi;
constexpr float invTwoPi = 1.f / twoPi;
constexpr float halfPi = pi / 2.f;
constexpr float twoByPi = 2.f / pi;
