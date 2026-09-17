#include "OutputAllocation.h"
#include <cstdio>
#include <cstdlib>
using namespace apparatus;
static int checks = 0;
static void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool identical(const OutputLayout& a, const OutputLayout& b) {
    return a.vactrol_count == b.vactrol_count && a.relay_count == b.relay_count &&
        a.vactrol_pins == b.vactrol_pins && a.relay_pins == b.relay_pins;
}
int main() {
    // Explicit mixed full layout, not populated through the allocator under test.
    OutputLayout full;
    full.vactrol_count = 7; full.relay_count = 8;
    full.vactrol_pins = {{13,14,25,26,27,33,5,-1,-1,-1,-1,-1,-1,-1}};
    full.relay_pins = {{4,18,19,21,22,23,32,15,-1,-1,-1,-1,-1,-1}};
    require(validateLayout(full).ok, "explicit 7+8 layout must be valid");
    const OutputLayout original = full;
    for (Bank bank : {Bank::Vactrol, Bank::Relay}) {
        require(!addOutput(full, bank).ok, "auto allocation must reject full shared pool");
        require(identical(full, original), "full-pool rejection must not mutate any slot");
        require(!addOutput(full, bank, 5).ok, "explicit allocation must reject full pool");
        require(identical(full, original), "explicit rejection must preserve layout");
    }
    require(findFreeOutputPin(full) == -1, "full layout has no free pin");
    require(remapOutput(full, Bank::Relay, 7, 15).ok, "same-pin remap works even when full");
    require(identical(full, original), "same-pin remap is idempotent");
    require(!remapOutput(full, Bank::Relay, 0, 13).ok, "relay cannot steal Mix pin");
    require(identical(full, original), "failed remap preserves all ownership");
    require(removeLastOutput(full, Bank::Vactrol).ok, "tail removal frees exactly one pin");
    require(full.vactrol_pins[6] == -1, "removed slot cleared");
    auto added = addOutput(full, Bank::Relay);
    require(added.ok && added.pin == 5, "other bank can reuse released pin");
    require(full.vactrol_count == 6 && full.relay_count == 9, "cross-bank reuse updates counts");
    require(validateLayout(full).ok, "resulting mixed layout remains valid");
    OutputLayout fresh;
    const OutputLayout before = fresh;
    require(!addOutput(fresh, Bank::Relay, 13).ok, "cross-bank conflict rejected below capacity");
    require(identical(fresh, before), "conflict rejection is transactional");
    require(!remapOutput(fresh, Bank::Vactrol, 0, 4).ok, "vactrol cannot steal relay pin");
    require(identical(fresh, before), "reverse conflict preserves layout");
    std::printf("Independent boundary review: PASS (%d checks)\n", checks);
}
