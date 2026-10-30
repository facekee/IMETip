#pragma once

#include <cstdio>

namespace imetip::test {

inline int& Failures() {
    static int value = 0;
    return value;
}

inline void Check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        ++Failures();
        std::printf("  FAIL %s:%d  %s\n", file, line, expression);
    }
}

} // namespace imetip::test

#define CHECK(expr) ::imetip::test::Check((expr), #expr, __FILE__, __LINE__)
