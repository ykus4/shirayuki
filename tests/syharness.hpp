// Minimal assertion harness — no external dependencies, so `cmake && ctest`
// works on a bare machine.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace syt {

inline int g_failures = 0;
inline int g_checks = 0;

inline void report(bool ok, const char *expr, const char *file, int line,
                   const std::string &detail = "") {
    ++g_checks;
    if (ok)
        return;
    ++g_failures;
    std::fprintf(stderr, "FAIL %s:%d: %s", file, line, expr);
    if (!detail.empty())
        std::fprintf(stderr, "  [%s]", detail.c_str());
    std::fprintf(stderr, "\n");
}

inline int summary(const char *name) {
    std::fprintf(stderr, "%s: %d checks, %d failures\n", name, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

} // namespace syt

#define SY_CHECK(expr) ::syt::report((expr), #expr, __FILE__, __LINE__)

#define SY_CHECK_EQ(a, b)                                                                          \
    do {                                                                                           \
        auto _a = (a);                                                                             \
        auto _b = (b);                                                                             \
        bool _ok = (_a == _b);                                                                     \
        std::string _d;                                                                            \
        if (!_ok) {                                                                                \
            std::ostringstream _os;                                                                \
            _os << "got " << _a << ", want " << _b;                                                \
            _d = _os.str();                                                                        \
        }                                                                                          \
        ::syt::report(_ok, #a " == " #b, __FILE__, __LINE__, _d);                                  \
    } while (0)

#define SY_MAIN(name)                                                                              \
    int main() {                                                                                   \
        run();                                                                                     \
        return ::syt::summary(name);                                                               \
    }
