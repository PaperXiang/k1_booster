#pragma once

#include <cstddef>
#include <deque>

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
    double max_acceleration = 0.0;
    bool allow_projection = false;
};

struct Result {
    Point3D measured_position;
    Point3D predicted_position;
    Point2D velocity;
    Point2D acceleration;
    bool prediction_applied = false;
    bool history_reset = false;
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

    void pushSample(const Point3D &position, double timestamp);

    Config config_;
    std::deque<Sample> history_;
};

} // namespace ball_motion_predictor
