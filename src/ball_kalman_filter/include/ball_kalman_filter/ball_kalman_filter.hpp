#pragma once

#include <array>

namespace ball_kalman_filter {

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

struct Config {
    double process_noise_position = 0.03;
    double process_noise_velocity = 0.60;
    double measurement_noise = 0.06;
    double initial_covariance = 1.0;
};

struct Result {
    Point2D position;
    Point2D velocity;
    bool initialized = false;
};

class BallKalmanFilter {
public:
    BallKalmanFilter() = default;
    explicit BallKalmanFilter(const Config &config);

    void setConfig(const Config &config);
    const Config &config() const;
    void reset();

    Result update(const Point2D &measured_position, double dt, bool valid_dt);
    bool initialized() const;
    Point2D position() const;
    Point2D velocity() const;

private:
    void initialize(const Point2D &position);

    Config config_;
    std::array<double, 4> state_{}; // x, y, vx, vy
    std::array<double, 16> covariance_{}; // row-major 4x4
    bool initialized_ = false;
};

} // namespace ball_kalman_filter
