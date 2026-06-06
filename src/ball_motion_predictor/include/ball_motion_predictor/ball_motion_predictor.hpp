#pragma once

#include <cstddef>
#include <deque>

#include "ball_kalman_filter/ball_kalman_filter.hpp"

namespace ball_motion_predictor {

struct Point3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

struct Config {
    bool enable = false;
    double predict_time = 0.25;
    double min_dt = 0.02;
    double max_dt = 0.20;
    double max_history_gap = 0.50;
    double max_speed = 4.0;
    double max_acceleration = 8.0;
    bool allow_projection = false;
    bool enable_kalman_filter = false;
    double kalman_process_noise_position = 0.03;
    double kalman_process_noise_velocity = 0.60;
    double kalman_measurement_noise = 0.06;
    double kalman_initial_covariance = 1.0;
};

struct Result {
    Point3D measured_position;
    Point3D filtered_position;
    Point3D predicted_position;
    Point2D filtered_velocity;
    Point2D velocity;
    Point2D acceleration;
    bool prediction_applied = false;
    bool history_reset = false;
    bool kalman_initialized = false;
};

class BallMotionPredictor {
public:
    BallMotionPredictor() = default;
    explicit BallMotionPredictor(const Config &config);

    void setConfig(const Config &config);
    const Config &config() const;
    void reset();

    Result update(const Point3D &measured_position, double timestamp, bool reliable_measurement);
    std::size_t historySize() const;

private:
    struct Sample {
        double timestamp = 0.0;
        Point3D position;
    };

    struct VelocitySample {
        double timestamp = 0.0;
        Point2D velocity;
    };

    void pushSample(const Point3D &position, double timestamp);
    void pushVelocitySample(const Point2D &velocity, double timestamp);

    Config config_;
    std::deque<Sample> history_;
    std::deque<VelocitySample> velocity_history_;
    ball_kalman_filter::BallKalmanFilter kalman_filter_;
};

} // namespace ball_motion_predictor
