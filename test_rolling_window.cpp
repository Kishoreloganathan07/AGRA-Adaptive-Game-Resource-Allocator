#include "test_framework.hpp"
#include "agra/analysis/rolling_window.hpp"

#include <cmath>

using namespace agra::analysis;

AGRA_TEST_CASE("Analysis - Rolling Window Mean Min Max") {
    RollingWindow win(5);
    AGRA_CHECK(win.empty());

    win.push(10.0);
    win.push(20.0);
    win.push(30.0);

    AGRA_CHECK_EQ(win.size(), 3u);
    AGRA_CHECK(std::abs(win.mean() - 20.0) < 1e-6);
    AGRA_CHECK(std::abs(win.min_value() - 10.0) < 1e-6);
    AGRA_CHECK(std::abs(win.max_value() - 30.0) < 1e-6);
    AGRA_CHECK(std::abs(win.latest() - 30.0) < 1e-6);
    AGRA_CHECK(std::abs(win.oldest() - 10.0) < 1e-6);

    // Push past capacity
    win.push(40.0);
    win.push(50.0);
    win.push(60.0); // Evicts 10.0

    AGRA_CHECK_EQ(win.size(), 5u);
    AGRA_CHECK(std::abs(win.oldest() - 20.0) < 1e-6);
    AGRA_CHECK(std::abs(win.latest() - 60.0) < 1e-6);
}

AGRA_TEST_CASE("Analysis - Rolling Window Linear Regression Slope") {
    RollingWindow win(5);
    // Flat line: slope should be 0
    win.push(10.0);
    win.push(10.0);
    win.push(10.0);
    AGRA_CHECK(std::abs(win.slope()) < 1e-6);

    // Strictly rising line: y = 2x
    RollingWindow rising(5);
    rising.push(0.0);
    rising.push(2.0);
    rising.push(4.0);
    rising.push(6.0);
    AGRA_CHECK(std::abs(rising.slope() - 2.0) < 1e-5);
}

AGRA_TEST_CASE("Analysis - EWMA Smoothing") {
    Ewma ewma(0.5);
    AGRA_CHECK(!ewma.has_value());

    ewma.update(100.0);
    AGRA_CHECK(ewma.has_value());
    AGRA_CHECK(std::abs(ewma.value() - 100.0) < 1e-6);

    ewma.update(50.0);
    // 0.5 * 50 + 0.5 * 100 = 75
    AGRA_CHECK(std::abs(ewma.value() - 75.0) < 1e-6);

    ewma.update(50.0);
    // 0.5 * 50 + 0.5 * 75 = 62.5
    AGRA_CHECK(std::abs(ewma.value() - 62.5) < 1e-6);
}
