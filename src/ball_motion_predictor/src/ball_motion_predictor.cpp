#include "ball_motion_predictor/ball_motion_predictor.hpp"

#include <algorithm>
#include <cmath>

namespace ball_motion_predictor {

namespace {

bool isFinitePosition(const Point3D &position) {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

Point2D limitVectorNorm(const Point2D &value, double max_norm) {
    if (max_norm <= 0.0) {
        return value;
    }

    double norm = std::hypot(value.x, value.y);
    if (norm <= max_norm || norm <= 1e-6) {
        return value;
    }

    double scale = max_norm / norm;
    return Point2D{value.x * scale, value.y * scale};
}

} // namespace

BallMotionPredictor::BallMotionPredictor(const Config &config) : config_(config) {}

void BallMotionPredictor::setConfig(const Config &config) {
    config_ = config;
    reset();
}

const Config &BallMotionPredictor::config() const {
    return config_;
}

void BallMotionPredictor::reset() {
    history_.clear();
    velocity_history_.clear();
}

std::size_t BallMotionPredictor::historySize() const {
    return history_.size();
}

void BallMotionPredictor::pushSample(const Point3D &position, double timestamp) {
    history_.push_back(Sample{timestamp, position});
    while (history_.size() > 2) {
        history_.pop_front();
    }
}

void BallMotionPredictor::pushVelocitySample(const Point2D &velocity, double timestamp) {
    velocity_history_.push_back(VelocitySample{timestamp, velocity});
    while (velocity_history_.size() > 2) {
        velocity_history_.pop_front();
    }
}

Result BallMotionPredictor::update(const Point3D &measured_position,
                                   double timestamp,
                                   bool reliable_measurement) {
    Result result;
    result.measured_position = measured_position;
    result.predicted_position = measured_position;

    if (!config_.enable || !reliable_measurement || !isFinitePosition(measured_position)) {
        result.history_reset = !history_.empty();
        reset();
        return result;
    }

    if (!history_.empty()) {
        double gap = timestamp - history_.back().timestamp;
        if (gap < 0.0 || gap > config_.max_history_gap) {
            result.history_reset = true;
            reset();
        }
    }

    if (history_.empty()) {
        pushSample(measured_position, timestamp);
        return result;
    }

    const auto &last = history_.back();
    double dt_current = timestamp - last.timestamp;
    if (dt_current < config_.min_dt || dt_current > config_.max_dt) {
        velocity_history_.clear();
        pushSample(measured_position, timestamp);
        return result;
    }

    result.velocity = Point2D{
        (measured_position.x - last.position.x) / dt_current,
        (measured_position.y - last.position.y) / dt_current};
    result.velocity = limitVectorNorm(result.velocity, config_.max_speed);

    double velocity_timestamp = 0.5 * (last.timestamp + timestamp);
    if (!velocity_history_.empty()) {
        const auto &prev_velocity = velocity_history_.back();
        double dt_velocity = velocity_timestamp - prev_velocity.timestamp;
        if (dt_velocity >= config_.min_dt && dt_velocity <= config_.max_dt) {
            result.acceleration = Point2D{
                (result.velocity.x - prev_velocity.velocity.x) / dt_velocity,
                (result.velocity.y - prev_velocity.velocity.y) / dt_velocity};
            result.acceleration = limitVectorNorm(result.acceleration, config_.max_acceleration);
        } else if (dt_velocity < 0.0 || dt_velocity > config_.max_history_gap) {
            velocity_history_.clear();
        }
    }

    double predict_time = std::max(0.0, config_.predict_time);
    result.predicted_position.x += result.velocity.x * predict_time +
                                   0.5 * result.acceleration.x * predict_time * predict_time;
    result.predicted_position.y += result.velocity.y * predict_time +
                                   0.5 * result.acceleration.y * predict_time * predict_time;

    pushSample(measured_position, timestamp);
    pushVelocitySample(result.velocity, velocity_timestamp);
    result.prediction_applied = predict_time > 1e-6 && isFinitePosition(result.predicted_position);
    if (!result.prediction_applied) {
        result.predicted_position = measured_position;
    }

    return result;
}

} // namespace ball_motion_predictor
