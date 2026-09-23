#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <format>
#include <chrono>
#include <type_traits>

namespace agra::test {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry reg;
        return reg;
    }

    void register_test(const std::string& name, std::function<void()> func) {
        tests_.push_back({name, std::move(func)});
    }

    int run_all() {
        int passed = 0;
        int failed = 0;
        std::cout << "\n============================================================\n";
        std::cout << " Running AGRA Test Suite (" << tests_.size() << " test cases)\n";
        std::cout << "============================================================\n\n";

        auto start = std::chrono::steady_clock::now();

        for (const auto& t : tests_) {
            std::cout << " [ RUN      ] " << t.name << "...\n";
            try {
                current_failed_ = false;
                t.func();
                if (!current_failed_) {
                    std::cout << " [\033[32m       PASS \033[0m] " << t.name << "\n";
                    ++passed;
                } else {
                    std::cout << " [\033[31m       FAIL \033[0m] " << t.name << "\n";
                    ++failed;
                }
            } catch (const std::exception& e) {
                std::cout << " [\033[31m  EXCEPTION \033[0m] " << t.name << ": " << e.what() << "\n";
                ++failed;
            } catch (...) {
                std::cout << " [\033[31m  EXCEPTION \033[0m] " << t.name << ": Unknown exception\n";
                ++failed;
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

        std::cout << "\n============================================================\n";
        std::cout << " Summary: " << passed << " passed, " << failed << " failed, in " << elapsed.count() << " ms\n";
        std::cout << "============================================================\n\n";

        return (failed == 0) ? 0 : 1;
    }

    void mark_failed() noexcept { current_failed_ = true; }
    [[nodiscard]] bool is_current_failed() const noexcept { return current_failed_; }

private:
    std::vector<TestCase> tests_;
    bool current_failed_{false};
};

struct AutoRegister {
    AutoRegister(const std::string& name, std::function<void()> func) {
        TestRegistry::instance().register_test(name, std::move(func));
    }
};

template <typename T>
auto to_printable(const T& val) {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<std::underlying_type_t<T>>(val);
    } else {
        return val;
    }
}

} // namespace agra::test

#define AGRA_CONCAT_IMPL(a, b) a##b
#define AGRA_CONCAT(a, b) AGRA_CONCAT_IMPL(a, b)

#define AGRA_TEST_CASE(name) \
    static void AGRA_CONCAT(test_func_, __LINE__)(); \
    static ::agra::test::AutoRegister AGRA_CONCAT(auto_reg_, __LINE__)(name, AGRA_CONCAT(test_func_, __LINE__)); \
    static void AGRA_CONCAT(test_func_, __LINE__)()

#define AGRA_CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::cout << "   \033[31mFAILED\033[0m: " #expr " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ::agra::test::TestRegistry::instance().mark_failed(); \
        } \
    } while(0)

#define AGRA_CHECK_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cout << "   \033[31mFAILED\033[0m: " #a " == " #b " (expected " \
                      << ::agra::test::to_printable(b) << ", got " << ::agra::test::to_printable(a) \
                      << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ::agra::test::TestRegistry::instance().mark_failed(); \
        } \
    } while(0)
