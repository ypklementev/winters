// Minimal single-header test harness (no external dependencies).
//
//   TEST(name) { CHECK(cond); CHECK_NEAR(a, b, eps); REQUIRE(cond); }
//
// Define MINI_TEST_MAIN in exactly one translation unit to get main().
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace mt {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

inline int& failures() {
    static int f = 0;
    return f;
}

struct RequireFailed {};

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void report(const char* file, int line, const std::string& what) {
    ++failures();
    std::printf("  FAILED %s:%d: %s\n", file, line, what.c_str());
}

inline int runAll(const char* filter) {
    int failedCases = 0;
    int ran = 0;
    for (const auto& c : registry()) {
        if (filter && std::string(c.name).find(filter) == std::string::npos) continue;
        ++ran;
        const int before = failures();
        try {
            c.fn();
        } catch (const RequireFailed&) {
        } catch (...) {
            report(__FILE__, __LINE__, "unexpected exception");
        }
        const bool ok = failures() == before;
        if (!ok) ++failedCases;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
    }
    std::printf("\n%d/%d test cases passed\n", ran - failedCases, ran);
    return failedCases == 0 ? 0 : 1;
}

}  // namespace mt

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)

#define TEST(name)                                               \
    static void name();                                          \
    static ::mt::Registrar MT_CAT(mt_reg_, name)(#name, &name);  \
    static void name()

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) ::mt::report(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define REQUIRE(cond)                                                      \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ::mt::report(__FILE__, __LINE__, "REQUIRE(" #cond ")");        \
            throw ::mt::RequireFailed{};                                   \
        }                                                                  \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                          \
    do {                                                                               \
        const double mt_a = static_cast<double>(a), mt_b = static_cast<double>(b);     \
        if (!(std::fabs(mt_a - mt_b) <= (eps))) {                                      \
            ::mt::report(__FILE__, __LINE__,                                           \
                         "CHECK_NEAR(" #a ", " #b ") " + std::to_string(mt_a) + " vs " + \
                             std::to_string(mt_b));                                    \
        }                                                                              \
    } while (0)

#ifdef MINI_TEST_MAIN
int main(int argc, char** argv) { return ::mt::runAll(argc > 1 ? argv[1] : nullptr); }
#endif
