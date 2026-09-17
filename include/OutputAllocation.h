#pragma once
#include <array>

namespace apparatus {
inline constexpr std::array<int,15> OUTPUT_PINS{{4,13,14,18,19,21,22,23,25,26,27,32,33,15,5}};
inline constexpr int BANK_CAPACITY = 14;
inline constexpr int LEDC_CAPACITY = 16;
enum class Bank { Vactrol, Relay };
struct OutputLayout {
    int vactrol_count = 1;
    int relay_count = 1;
    std::array<int,14> vactrol_pins{{13,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}};
    std::array<int,14> relay_pins{{4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}};
};
struct AllocationResult { bool ok; const char* error; int pin; };
bool isOutputPin(int pin);
bool isStrapPin(int pin);
AllocationResult validateLayout(const OutputLayout& layout);
int findFreeOutputPin(const OutputLayout& layout);
AllocationResult addOutput(OutputLayout& layout, Bank bank, int requested_pin = -1);
AllocationResult removeLastOutput(OutputLayout& layout, Bank bank);
AllocationResult remapOutput(OutputLayout& layout, Bank bank, int index, int pin);
// Pure topology: no GPIO, NVS, global configuration, or boot-reference checks.
// Callers validate boot references/settings and commit hardware separately.
// Vactrol index is its LEDC channel. Inactive cells never own pins.
// GPIO5/15 are strap-sensitive: software cannot guarantee boot safety.
// Require documented physical pulls/isolated drivers when using these pins.
} // namespace apparatus
