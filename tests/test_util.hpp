#ifndef AKIRA_TEST_UTIL_HPP
#define AKIRA_TEST_UTIL_HPP

#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace tests {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();
extern int failures;

struct Register {
    Register(const char* name, std::function<void()> body)
    {
        registry().push_back({name, std::move(body)});
    }
};

} // namespace tests

#define TEST(name)                                                          \
    static void name();                                                     \
    static tests::Register register_##name(#name, name);                    \
    static void name()

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("      %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            tests::failures++;                                              \
        }                                                                   \
    } while (0)

#define CHECK_EQ(actual, expected)                                          \
    do {                                                                    \
        auto actualValue = (actual);                                        \
        auto expectedValue = (expected);                                    \
        if (!(actualValue == expectedValue)) {                              \
            std::printf("      %s:%d: %s\n", __FILE__, __LINE__, #actual);  \
            std::cout << "        expected: " << expectedValue              \
                      << "\n        actual:   " << actualValue << "\n";     \
            tests::failures++;                                              \
        }                                                                   \
    } while (0)

#endif // AKIRA_TEST_UTIL_HPP
