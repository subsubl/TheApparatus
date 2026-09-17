#include "OutputAllocation.h"

namespace apparatus {
namespace {
AllocationResult fail(const char* error, int pin = -1) { return {false,error,pin}; }
AllocationResult success(int pin = -1) { return {true,nullptr,pin}; }
bool validBank(Bank bank) { return bank == Bank::Vactrol || bank == Bank::Relay; }
int& count(OutputLayout& l, Bank b) {
    return b == Bank::Vactrol ? l.vactrol_count : l.relay_count;
}
std::array<int,14>& pins(OutputLayout& l, Bank b) {
    return b == Bank::Vactrol ? l.vactrol_pins : l.relay_pins;
}
// Internal callers have already validated counts.
bool owned(const OutputLayout& l, int pin) {
    for (int i = 0; i < l.vactrol_count; ++i) if (l.vactrol_pins[i] == pin) return true;
    for (int i = 0; i < l.relay_count; ++i) if (l.relay_pins[i] == pin) return true;
    return false;
}
} // namespace
bool isOutputPin(int pin) {
    for (int candidate : OUTPUT_PINS) if (candidate == pin) return true;
    return false;
}
bool isStrapPin(int pin) { return pin == 5 || pin == 15; }
AllocationResult validateLayout(const OutputLayout& l) {
    if (l.vactrol_count < 1 || l.vactrol_count > BANK_CAPACITY ||
        l.relay_count < 1 || l.relay_count > BANK_CAPACITY)
        return fail("bank count out of range");
    if (l.vactrol_count > LEDC_CAPACITY) return fail("LEDC capacity exceeded");
    if (l.vactrol_count + l.relay_count > static_cast<int>(OUTPUT_PINS.size()))
        return fail("shared output capacity exceeded");
    std::array<int,15> seen{};
    int used = 0;
    for (int bank = 0; bank < 2; ++bank) {
        const auto& p = bank == 0 ? l.vactrol_pins : l.relay_pins;
        const int n = bank == 0 ? l.vactrol_count : l.relay_count;
        for (int i = 0; i < n; ++i) {
            if (!isOutputPin(p[i])) return fail("invalid output pin",p[i]);
            for (int j = 0; j < used; ++j)
                if (seen[j] == p[i]) return fail("output pin already owned",p[i]);
            seen[used++] = p[i];
        }
    }
    return success();
}
int findFreeOutputPin(const OutputLayout& l) {
    if (!validateLayout(l).ok) return -1;
    for (int pin : OUTPUT_PINS) if (!owned(l,pin)) return pin;
    return -1;
}
AllocationResult addOutput(OutputLayout& l, Bank b, int requested_pin) {
    const auto valid = validateLayout(l);
    if (!valid.ok) return valid;
    if (!validBank(b)) return fail("invalid bank");
    const int pin = requested_pin == -1 ? findFreeOutputPin(l) : requested_pin;
    if (count(l,b) >= BANK_CAPACITY) return fail("bank capacity exceeded",pin);
    if (l.vactrol_count + l.relay_count >= static_cast<int>(OUTPUT_PINS.size()))
        return fail("shared output capacity exceeded",pin);
    if (b == Bank::Vactrol && count(l,b) >= LEDC_CAPACITY)
        return fail("LEDC capacity exceeded",pin);
    if (!isOutputPin(pin)) return fail("invalid output pin",pin);
    if (owned(l,pin)) return fail("output pin already owned",pin);
    pins(l,b)[count(l,b)] = pin;
    ++count(l,b);
    return success(pin);
}
AllocationResult removeLastOutput(OutputLayout& l, Bank b) {
    const auto valid = validateLayout(l);
    if (!valid.ok) return valid;
    if (!validBank(b)) return fail("invalid bank");
    if (count(l,b) <= 1) return fail("first output cannot be removed");
    const int index = count(l,b) - 1;
    const int pin = pins(l,b)[index];
    pins(l,b)[index] = -1;
    --count(l,b);
    return success(pin);
}
AllocationResult remapOutput(OutputLayout& l, Bank b, int index, int pin) {
    const auto valid = validateLayout(l);
    if (!valid.ok) return valid;
    if (!validBank(b)) return fail("invalid bank",pin);
    if (index < 0 || index >= count(l,b)) return fail("output index out of range",pin);
    if (!isOutputPin(pin)) return fail("invalid output pin",pin);
    if (pins(l,b)[index] == pin) return success(pin);
    if (owned(l,pin)) return fail("output pin already owned",pin);
    pins(l,b)[index] = pin;
    return success(pin);
}
} // namespace apparatus
