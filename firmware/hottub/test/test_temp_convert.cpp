#include "../temp_convert.h"
#include <cassert>
#include <cstdio>
#include <cmath>

int main() {
    assert(std::fabs(fToC(104) - 40.0f) < 0.05f);
    assert(std::fabs(fToC(80)  - 26.667f) < 0.05f);
    assert(cToF(40.0f) == 104);
    assert(cToF(26.5f) == 80);          // 26.5*1.8+32 = 79.7 -> 80
    assert(cToF(fToC(102)) == 102);     // round-trip stays put
    assert(cToF(fToC(98))  == 98);
    printf("ALL TESTS PASSED\n");
    return 0;
}
