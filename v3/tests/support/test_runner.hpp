#pragma once

#include <cstdio>
#include <exception>
#include <initializer_list>

namespace switch_auto::test {

struct TestCase {
    const char* name;
    void (*run)(class TestContext&);
};

class TestContext {
public:
    void expect(bool condition, const char* expression, const char* file,
                int line) noexcept {
        ++checks_;
        if (!condition) {
            std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
            ++failures_;
        }
    }

    [[nodiscard]] int checks() const noexcept { return checks_; }
    [[nodiscard]] int failures() const noexcept { return failures_; }

private:
    int checks_ = 0;
    int failures_ = 0;
};

// This runner intentionally does not use assert(): checks remain enabled in
// Release builds and a failing test always produces a non-zero exit code.
inline int runTests(std::initializer_list<TestCase> tests) noexcept {
    TestContext context;
    for (const auto& test : tests) {
        const auto failures_before = context.failures();
        try {
            test.run(context);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL %s: unexpected exception: %s\n",
                         test.name, error.what());
            context.expect(false, "test must not throw", __FILE__, __LINE__);
        } catch (...) {
            std::fprintf(stderr, "FAIL %s: unexpected non-standard exception\n",
                         test.name);
            context.expect(false, "test must not throw", __FILE__, __LINE__);
        }

        std::fprintf(stderr, "%s %s\n",
                     failures_before == context.failures() ? "PASS" : "FAIL",
                     test.name);
    }

    std::fprintf(stderr, "%d checks, %d failures\n", context.checks(),
                 context.failures());
    return context.failures() == 0 ? 0 : 1;
}

}  // namespace switch_auto::test

#define SA_TEST_EXPECT(context, condition)                                    \
    (context).expect(static_cast<bool>(condition), #condition, __FILE__,      \
                     __LINE__)
