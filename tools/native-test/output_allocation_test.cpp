#include "OutputAllocation.h"
#include <climits>
#include <cstdio>
using namespace apparatus;
namespace {
int assertions = 0, failures = 0;
void check(bool ok, const char* text, int line) {
    ++assertions;
    if (!ok) { ++failures; std::printf("FAIL line %d: %s\n",line,text); }
}
#define CHECK(x) check(static_cast<bool>(x),#x,__LINE__)
bool equal(const OutputLayout& a, const OutputLayout& b) {
    return a.vactrol_count == b.vactrol_count && a.relay_count == b.relay_count &&
           a.vactrol_pins == b.vactrol_pins && a.relay_pins == b.relay_pins;
}
void rejected(const OutputLayout& before, const OutputLayout& after, AllocationResult r) {
    CHECK(!r.ok); CHECK(r.error != nullptr);
    if (r.error) CHECK(r.error[0] != '\0');
    CHECK(equal(before,after));
}
AllocationResult badAdd(OutputLayout& l, Bank b, int pin = -1) {
    const auto before = l; const auto r = addOutput(l,b,pin); rejected(before,l,r); return r;
}
AllocationResult badRemap(OutputLayout& l, Bank b, int index, int pin) {
    const auto before = l; const auto r = remapOutput(l,b,index,pin); rejected(before,l,r); return r;
}
void badRemove(OutputLayout& l, Bank b) {
    const auto before = l; const auto r = removeLastOutput(l,b); rejected(before,l,r);
}
void good(AllocationResult r, int pin) { CHECK(r.ok); CHECK(r.error == nullptr); CHECK(r.pin == pin); }
void invalidLayout(OutputLayout l) {
    const auto before = l;
    const auto r = validateLayout(l); rejected(before,l,r);
    CHECK(findFreeOutputPin(l) == -1); CHECK(equal(before,l));
    for (Bank b : {Bank::Vactrol,Bank::Relay}) {
        badAdd(l,b); badRemove(l,b); badRemap(l,b,0,14);
    }
}
} // namespace
int main() {
    constexpr Bank V = Bank::Vactrol, R = Bank::Relay;
    const std::array<int,15> expected{{4,13,14,18,19,21,22,23,25,26,27,32,33,15,5}};
    CHECK(OUTPUT_PINS == expected); CHECK(BANK_CAPACITY == 14); CHECK(LEDC_CAPACITY == 16);
    for (int p : expected) { CHECK(isOutputPin(p)); CHECK(isStrapPin(p) == (p == 5 || p == 15)); }
    OutputLayout l;
    good(validateLayout(l),-1);
    CHECK(l.vactrol_count == 1); CHECK(l.relay_count == 1);
    CHECK(l.vactrol_pins[0] == 13); CHECK(l.relay_pins[0] == 4);
    for (int i = 1; i < BANK_CAPACITY; ++i) { CHECK(l.vactrol_pins[i] == -1); CHECK(l.relay_pins[i] == -1); }
    CHECK(findFreeOutputPin(l) == 14);
    const int invalidPins[] = {INT_MIN,-999,-2,0,1,2,3,6,7,8,9,10,11,12,16,17,20,24,28,29,30,31,34,35,36,37,38,39,40,256,999,INT_MAX};
    for (int p : invalidPins) {
        CHECK(!isOutputPin(p)); CHECK(!isStrapPin(p));
        for (Bank b : {V,R}) {
            CHECK(badAdd(l,b,p).pin == p); CHECK(badRemap(l,b,0,p).pin == p);
        }
        auto corrupt = l; corrupt.vactrol_pins[0] = p; invalidLayout(corrupt);
    }
    CHECK(!isOutputPin(-1)); CHECK(!isStrapPin(-1));
    badRemap(l,V,0,-1);
    auto corrupt = l; corrupt.relay_pins[0] = -1; invalidLayout(corrupt);
    for (Bank b : {V,R}) { badAdd(l,b,13); badAdd(l,b,4); badRemove(l,b); }
    badRemap(l,V,0,4); badRemap(l,R,0,13);
    const auto defaults = l;
    good(remapOutput(l,V,0,13),13); good(remapOutput(l,R,0,4),4); CHECK(equal(defaults,l));
    good(remapOutput(l,V,0,14),14); CHECK(l.vactrol_pins[0] == 14);
    good(remapOutput(l,R,0,13),13); CHECK(l.relay_pins[0] == 13);
    l = defaults;
    good(addOutput(l,V),14); good(addOutput(l,R),18);
    badRemap(l,V,1,13); badRemap(l,R,1,4); badRemap(l,V,1,18); badRemap(l,R,1,14);
    for (int i : {INT_MIN,-1,2,14,256,999,INT_MAX}) { badRemap(l,V,i,19); badRemap(l,R,i,19); }
    for (int b : {-1,2,256,999}) {
        const Bank forged = static_cast<Bank>(b);
        badAdd(l,forged); badAdd(l,forged,19); badRemove(l,forged); badRemap(l,forged,0,19);
    }
    good(removeLastOutput(l,V),14); CHECK(l.vactrol_pins[1] == -1); CHECK(l.vactrol_count == 1);
    CHECK(l.relay_pins[1] == 18); good(addOutput(l,R),14);
    good(removeLastOutput(l,R),14); CHECK(l.relay_pins[2] == -1);
    good(removeLastOutput(l,R),18); CHECK(l.relay_pins[1] == -1); CHECK(equal(l,defaults));
    badRemove(l,V); badRemove(l,R);
    l.vactrol_pins[8] = 14; l.relay_pins[9] = 999;
    good(validateLayout(l),-1); CHECK(findFreeOutputPin(l) == 14);
    good(addOutput(l,R),14); CHECK(l.vactrol_pins[8] == 14); CHECK(l.relay_pins[9] == 999);
    badAdd(l,V,14);
    l = defaults;
    for (int i = 2; i < 15; ++i) good(addOutput(l,(i % 2) ? R : V),expected[i]);
    CHECK(l.vactrol_count + l.relay_count == 15); CHECK(findFreeOutputPin(l) == -1);
    good(validateLayout(l),-1); badAdd(l,V); badAdd(l,R); badAdd(l,V,5);
    for (Bank b : {V,R}) {
        l = defaults;
        for (int i = 2; i < 15; ++i) good(addOutput(l,b),expected[i]);
        CHECK(l.vactrol_count == (b == V ? 14 : 1)); CHECK(l.relay_count == (b == R ? 14 : 1));
        CHECK(l.vactrol_count <= LEDC_CAPACITY); good(validateLayout(l),-1);
        badAdd(l,V); badAdd(l,R); badAdd(l,b,14);
        good(removeLastOutput(l,b),5); CHECK(findFreeOutputPin(l) == 5); good(addOutput(l,b),5);
    }
    for (int n : {INT_MIN,-1,0,15,256,999,INT_MAX}) {
        l = defaults; l.vactrol_count = n; invalidLayout(l);
        l = defaults; l.relay_count = n; invalidLayout(l);
    }
    l = defaults; l.vactrol_count = 8; l.relay_count = 8; invalidLayout(l);
    l = defaults; l.vactrol_count = 2; l.vactrol_pins[1] = 13; invalidLayout(l);
    l = defaults; l.relay_count = 2; l.relay_pins[1] = 4; invalidLayout(l);
    l = defaults; l.relay_pins[0] = 13; invalidLayout(l);
    l = defaults; l.vactrol_count = 2; invalidLayout(l);
    l = defaults; l.vactrol_count = 6; l.relay_count = 8;
    const int legacyV[] = {13,14,25,26,27,33};
    const int legacyR[] = {4,18,19,21,22,23,32,15};
    for (int i = 0; i < 6; ++i) l.vactrol_pins[i] = legacyV[i];
    for (int i = 0; i < 8; ++i) l.relay_pins[i] = legacyR[i];
    const auto legacy = l;
    good(validateLayout(l),-1); CHECK(equal(legacy,l)); CHECK(findFreeOutputPin(l) == 5);
    CHECK(isStrapPin(l.relay_pins[7])); CHECK(isStrapPin(findFreeOutputPin(l)));
    good(addOutput(l,V),5); CHECK(findFreeOutputPin(l) == -1);
    good(removeLastOutput(l,V),5); CHECK(equal(legacy,l));
    std::printf("WARNING: GPIO5/15 are strap-sensitive; require documented physical pulls/isolated drivers. Software cannot guarantee boot safety.\n");
    std::printf("%s: %d assertions, %d failures\n",failures ? "FAIL" : "PASS",assertions,failures);
    return failures ? 1 : 0;
}
