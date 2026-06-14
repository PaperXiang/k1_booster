#pragma once

#include <cstddef>
#include <vector>

#include <rclcpp/rclcpp.hpp>

namespace k1_ball_predictor {

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
    bool enable_kalman = true;
    double process_noise_position = 0.03;
    double process_noise_velocity = 0.60;
    double measurement_noise = 0.06;
    double initial_covariance = 1.0;
    double lost_prediction_timeout = 0.4;
    double max_jump_distance = 1.2;
    double max_jump_speed = 6.0;
    double trajectory_step = 0.1;
    int trajectory_count = 20;
};

struct Observation {
    rclcpp::Time stamp;
    Point2D position;
    double confidence = 0.0;
    bool reliable = false;
};

struct Result {
    bool valid = false;
    bool predicted_only = false;
    bool history_reset = false;
    bool outlier_rejected = false;
    Point2D raw_position;
    Point2D filtered_position;
    Point2D predicted_position;
    Point2D velocity;
    Point2D acceleration;
    std::vector<Point2D> trajectory;
    rclcpp::Time stamp;
};

class BallMotionPredictor {
public:
    BallMotionPredictor() = default;
    explicit BallMotionPredictor(const Config &config);

    void setConfig(const Config &config);
    const Config &config() const;
    void reset();
    Result update(const Observation &observation);

private:
    struct KalmanState {
        double x = 0.0;
        double y = 0.0;
        double vx = 0.0;
        double vy = 0.0;
        double covariance[4][4] = {};
        bool initialized = false;
    };

    void initializeKalman(const Point2D &position);
    void predictKalman(double dt);
    void correctKalman(const Point2D &position);
    Result predictOnly(const rclcpp::Time &stamp);
    bool isOutlier(const Observation &observation, double dt) const;
    std::vector<Point2D> buildTrajectory(const Point2D &start,
                                         const Point2D &velocity,
                                         const Point2D &acceleration) const;

    Config config_;
    KalmanState kalman_;
    bool has_last_sample_ = false;
    bool has_last_velocity_ = false;
    rclcpp::Time last_stamp_;
    Point2D last_position_;
    Point2D last_velocity_;
};

} // namespace k1_ball_predictor
