#include "agra/analysis/rolling_window.hpp"

#include <algorithm>

namespace agra::analysis {

RollingWindow::RollingWindow(std::size_t max_capacity)
    : max_capacity_((std::max)(static_cast<std::size_t>(2), max_capacity)) {}

void RollingWindow::push(double value) {
    if (buffer_.size() >= max_capacity_) {
        buffer_.pop_front();
    }
    buffer_.push_back(value);
}

void RollingWindow::clear() {
    buffer_.clear();
}

double RollingWindow::mean() const {
    if (buffer_.empty()) return 0.0;
    double sum = std::accumulate(buffer_.begin(), buffer_.end(), 0.0);
    return sum / static_cast<double>(buffer_.size());
}

double RollingWindow::variance() const {
    if (buffer_.size() < 2) return 0.0;
    double m = mean();
    double sum_sq = 0.0;
    for (double val : buffer_) {
        double diff = val - m;
        sum_sq += diff * diff;
    }
    return sum_sq / static_cast<double>(buffer_.size());
}

double RollingWindow::standard_deviation() const {
    return std::sqrt(variance());
}

double RollingWindow::min_value() const {
    if (buffer_.empty()) return 0.0;
    return *std::min_element(buffer_.begin(), buffer_.end());
}

double RollingWindow::max_value() const {
    if (buffer_.empty()) return 0.0;
    return *std::max_element(buffer_.begin(), buffer_.end());
}

double RollingWindow::latest() const {
    if (buffer_.empty()) return 0.0;
    return buffer_.back();
}

double RollingWindow::oldest() const {
    if (buffer_.empty()) return 0.0;
    return buffer_.front();
}

double RollingWindow::slope() const {
    std::size_t n = buffer_.size();
    if (n < 2) return 0.0;

    double sum_t = 0.0;
    double sum_x = 0.0;
    double sum_tx = 0.0;
    double sum_t2 = 0.0;

    for (std::size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i);
        double x = buffer_[i];
        sum_t += t;
        sum_x += x;
        sum_tx += t * x;
        sum_t2 += t * t;
    }

    double denominator = static_cast<double>(n) * sum_t2 - sum_t * sum_t;
    if (std::abs(denominator) < 1e-9) return 0.0;

    return (static_cast<double>(n) * sum_tx - sum_t * sum_x) / denominator;
}

Ewma::Ewma(double alpha) : alpha_(std::clamp(alpha, 0.01, 0.99)) {}

void Ewma::update(double sample) {
    if (!initialized_) {
        value_ = sample;
        initialized_ = true;
    } else {
        value_ = alpha_ * sample + (1.0 - alpha_) * value_;
    }
}

void Ewma::reset() {
    value_ = 0.0;
    initialized_ = false;
}

} // namespace agra::analysis
