#pragma once
// Minimal test helpers: CHECK / CHECK_EQ record failures, and `return summary()` from main.
#include <iostream>
#include <string>

namespace check {
inline int failures = 0;
inline int total = 0;
inline int summary() {
    std::cout << (total - failures) << "/" << total << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
}  // namespace check

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++check::total;                                                               \
        if (!(cond)) {                                                                \
            ++check::failures;                                                        \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        ++check::total;                                                                 \
        const auto va_ = (a);                                                          \
        const auto vb_ = (b);                                                          \
        if (!(va_ == vb_)) {                                                            \
            ++check::failures;                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b \
                      << "\n  got: " << va_ << "\n  want: " << vb_ << "\n";             \
        }                                                                               \
    } while (0)
