#include "ball_kalman_filter/ball_kalman_filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ball_kalman_filter {

namespace {

std::size_t matrixIndex(std::size_t row, std::size_t col) {
    return row * 4 + col;
}

bool isFinite(const Point2D &point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

} // namespace

BallKalmanFilter::BallKalmanFilter(const Config &config) : config_(config) {}

void BallKalmanFilter::setConfig(const Config &config) {
    config_ = config;
    reset();
}

const Config &BallKalmanFilter::config() const {
    return config_;
}

void BallKalmanFilter::reset() {
    state_.fill(0.0);
    covariance_.fill(0.0);
    initialized_ = false;
}

bool BallKalmanFilter::initialized() const {
    return initialized_;
}

Point2D BallKalmanFilter::position() const {
    return Point2D{state_[0], state_[1]};
}

Point2D BallKalmanFilter::velocity() const {
    return Point2D{state_[2], state_[3]};
}

void BallKalmanFilter::initialize(const Point2D &position) {
    state_ = {position.x, position.y, 0.0, 0.0};
    covariance_.fill(0.0);

    double initial_covariance = std::max(1e-9, config_.initial_covariance);
    for (std::size_t i = 0; i < 4; ++i) {
        covariance_[matrixIndex(i, i)] = initial_covariance;
    }
    initialized_ = true;
}

Result BallKalmanFilter::update(const Point2D &measured_position, double dt, bool valid_dt) {
    if (!isFinite(measured_position)) {
        return Result{position(), velocity(), initialized_};
    }

    if (!initialized_) {
        initialize(measured_position);
        return Result{position(), velocity(), initialized_};
    }

    if (valid_dt && dt > 1e-6) {
        state_[0] += state_[2] * dt;
        state_[1] += state_[3] * dt;

        std::array<double, 16> transition{};
        for (std::size_t i = 0; i < 4; ++i) {
            transition[matrixIndex(i, i)] = 1.0;
        }
        transition[matrixIndex(0, 2)] = dt;
        transition[matrixIndex(1, 3)] = dt;

        std::array<double, 16> temp{};
        std::array<double, 16> predicted_covariance{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t col = 0; col < 4; ++col) {
                for (std::size_t k = 0; k < 4; ++k) {
                    temp[matrixIndex(row, col)] += transition[matrixIndex(row, k)] *
                                                   covariance_[matrixIndex(k, col)];
                }
            }
        }

        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t col = 0; col < 4; ++col) {
                for (std::size_t k = 0; k < 4; ++k) {
                    predicted_covariance[matrixIndex(row, col)] += temp[matrixIndex(row, k)] *
                                                                    transition[matrixIndex(col, k)];
                }
            }
        }

        double process_noise_position = std::max(0.0, config_.process_noise_position);
        double process_noise_velocity = std::max(0.0, config_.process_noise_velocity);
        predicted_covariance[matrixIndex(0, 0)] += process_noise_position * dt * dt;
        predicted_covariance[matrixIndex(1, 1)] += process_noise_position * dt * dt;
        predicted_covariance[matrixIndex(2, 2)] += process_noise_velocity * dt;
        predicted_covariance[matrixIndex(3, 3)] += process_noise_velocity * dt;
        covariance_ = predicted_covariance;
    }

    double measurement_noise = std::max(1e-9, config_.measurement_noise);
    double s00 = covariance_[matrixIndex(0, 0)] + measurement_noise;
    double s01 = covariance_[matrixIndex(0, 1)];
    double s10 = covariance_[matrixIndex(1, 0)];
    double s11 = covariance_[matrixIndex(1, 1)] + measurement_noise;
    double det = s00 * s11 - s01 * s10;
    if (std::fabs(det) <= 1e-12) {
        return Result{position(), velocity(), initialized_};
    }

    double inv_s00 = s11 / det;
    double inv_s01 = -s01 / det;
    double inv_s10 = -s10 / det;
    double inv_s11 = s00 / det;
    double residual_x = measured_position.x - state_[0];
    double residual_y = measured_position.y - state_[1];

    std::array<double, 8> kalman_gain{}; // row-major 4x2
    for (std::size_t row = 0; row < 4; ++row) {
        double p0 = covariance_[matrixIndex(row, 0)];
        double p1 = covariance_[matrixIndex(row, 1)];
        kalman_gain[row * 2] = p0 * inv_s00 + p1 * inv_s10;
        kalman_gain[row * 2 + 1] = p0 * inv_s01 + p1 * inv_s11;
        state_[row] += kalman_gain[row * 2] * residual_x +
                       kalman_gain[row * 2 + 1] * residual_y;
    }

    std::array<double, 16> updated_covariance{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            double identity = row == col ? 1.0 : 0.0;
            double correction = 0.0;
            if (col == 0) {
                correction = kalman_gain[row * 2];
            } else if (col == 1) {
                correction = kalman_gain[row * 2 + 1];
            }
            double factor = identity - correction;
            for (std::size_t k = 0; k < 4; ++k) {
                updated_covariance[matrixIndex(row, k)] += factor * covariance_[matrixIndex(col, k)];
            }
        }
    }
    covariance_ = updated_covariance;

    return Result{position(), velocity(), initialized_};
}

} // namespace ball_kalman_filter
