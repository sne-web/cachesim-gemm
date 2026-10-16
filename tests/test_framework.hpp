// A tiny self-contained test framework, so the project builds with no
// downloads or third-party dependencies.
#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace testing {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures_in_current_test() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

template <class A, class B>
void check_equal(const A& actual, const B& expected, const char* actual_text, const char* expected_text,
                 const char* file, int line) {
    if (actual == expected) return;
    ++failures_in_current_test();
    std::cout << "    " << file << ":" << line << ": " << actual_text << " == " << expected_text << " failed (got "
              << actual << ", expected " << expected << ")\n";
}

inline void check_true(bool condition, const char* text, const char* file, int line) {
    if (condition) return;
    ++failures_in_current_test();
    std::cout << "    " << file << ":" << line << ": " << text << " failed\n";
}

inline int run_all() {
    int failed = 0;
    for (const TestCase& test : registry()) {
        failures_in_current_test() = 0;
        try {
            test.body();
        } catch (const std::exception& error) {
            ++failures_in_current_test();
            std::cout << "    unexpected exception: " << error.what() << "\n";
        }
        const bool ok = failures_in_current_test() == 0;
        std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << test.name << "\n";
        if (!ok) ++failed;
    }
    std::cout << "\n" << registry().size() - failed << " of " << registry().size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace testing

#define TEST(name)                                                     \
    static void test_##name();                                         \
    static const testing::Registrar registrar_##name(#name, test_##name); \
    static void test_##name()

#define CHECK(condition) testing::check_true((condition), #condition, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected) testing::check_equal((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define CHECK_THROWS(expression)                                                        \
    do {                                                                                \
        bool threw = false;                                                             \
        try {                                                                           \
            (void)(expression);                                                         \
        } catch (const std::exception&) {                                               \
            threw = true;                                                               \
        }                                                                               \
        testing::check_true(threw, #expression " throws", __FILE__, __LINE__);          \
    } while (false)
