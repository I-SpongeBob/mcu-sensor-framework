/**
 * @file test_support.hpp
 * @brief A 60-line test harness.
 *
 * No GoogleTest on purpose: the framework is built with -fno-exceptions and
 * must stay buildable in an offline, minimal environment. Macros count checks,
 * report the first failing line and set the process exit code, which is all
 * ctest needs.
 */
#ifndef SENSORFW_TESTS_SUPPORT_HPP
#define SENSORFW_TESTS_SUPPORT_HPP

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace testing {

inline int& failures() { static int value = 0; return value; }
inline int& checks()   { static int value = 0; return value; }

inline void reportFailure(const char* file, int line, const char* expression) {
    ++failures();
    printf("  FAIL %s:%d  %s\n", file, line, expression);
}

inline bool nearlyEqual(double a, double b, double tolerance) {
    return fabs(a - b) <= tolerance;
}

} // namespace testing

#define CHECK(expr)                                                            \
    do {                                                                       \
        ++testing::checks();                                                   \
        if (!(expr)) { testing::reportFailure(__FILE__, __LINE__, #expr); }    \
    } while (0)

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        ++testing::checks();                                                   \
        if (!((actual) == (expected))) {                                       \
            testing::reportFailure(__FILE__, __LINE__,                         \
                                   #actual " == " #expected);                  \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                \
    do {                                                                       \
        ++testing::checks();                                                   \
        if (!testing::nearlyEqual(static_cast<double>(actual),                 \
                                  static_cast<double>(expected),               \
                                  static_cast<double>(tolerance))) {           \
            printf("  FAIL %s:%d  %s = %.4f, expected %.4f +/- %.4f\n",        \
                   __FILE__, __LINE__, #actual,                                \
                   static_cast<double>(actual),                                \
                   static_cast<double>(expected),                              \
                   static_cast<double>(tolerance));                            \
            ++testing::failures();                                             \
        }                                                                      \
    } while (0)

#define CHECK_STREQ(actual, expected)                                          \
    do {                                                                       \
        ++testing::checks();                                                   \
        if (strcmp((actual), (expected)) != 0) {                               \
            testing::reportFailure(__FILE__, __LINE__,                         \
                                   #actual " == " #expected);                  \
        }                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do { printf("- %s\n", #fn); fn(); } while (0)

#define TEST_SUMMARY(suiteName)                                                \
    do {                                                                       \
        printf("%s: %d checks, %d failures\n", suiteName,                      \
               testing::checks(), testing::failures());                        \
        return (testing::failures() == 0) ? 0 : 1;                             \
    } while (0)

#endif // SENSORFW_TESTS_SUPPORT_HPP
