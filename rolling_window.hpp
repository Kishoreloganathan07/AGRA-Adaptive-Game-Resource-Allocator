#pragma once

#include <vector>
#include <deque>
#include <numeric>
#include <cmath>
#include <cstddef>
#include <optional>

namespace agra::analysis {

// Generic Rolling Window with statistical aggregation (mean, min, max, variance, stddev)
class RollingWindow {
public:
    explicit RollingWindow(std::size_t max_capacity = 20);

    void push(double value);
    void clear();

    [[nodiscard]] std::size_t size() const noexcept { return buffer_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return max_capacity_; }
    [[nodiscard]] bool empty() const noexcept { return buffer_.empty(); }
    [[nodiscard]] bool is_full() const noexcept { return buffer_.size() >= max_capacity_; }

    [[nodiscard]] double mean() const;
    [[nodiscard]] double variance() const;
    [[nodiscard]] double standard_deviation() const;
    [[nodiscard]] double min_value() const;
    [[nodiscard]] double max_value() const;
    [[nodiscard]] double latest() const;
    [[nodiscard]] double oldest() const;

    // Linear regression slope (rate of change per sample)
    [[nodiscard]] double slope() const;

private:
    std::size_t max_capacity_{20};
    std::deque<double> buffer_;
};

// Exponentially Weighted Moving Average
class Ewma {
public:
    explicit Ewma(double alpha = 0.25);

    void update(double sample);
    void reset();

    [[nodiscard]] double value() const noexcept { return value_; }
    [[nodiscard]] bool has_value() const noexcept { return initialized_; }

private:
    double alpha_{0.25};
    double value_{0.0};
    bool initialized_{false};
};

} // namespace agra::analysis
