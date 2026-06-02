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
        pushSample(measured_position, timestamp);
        return result;
    }

    Point2D raw_velocity{
        (measured_position.x - last.position.x) / dt_current,
        (measured_position.y - last.position.y) / dt_current};

    if (config_.max_speed > 0.0 && std::hypot(raw_velocity.x, raw_velocity.y) > config_.max_speed) {
        result.history_reset = true;
        reset();
        return result;
    }

    result.velocity = limitVectorNorm(raw_velocity, config_.max_speed);

    if (config_.max_acceleration > 0.0 && history_.size() >= 2) {
        const auto &prev = history_.front();
        double dt_prev = last.timestamp - prev.timestamp;
        if (dt_prev >= config_.min_dt && dt_prev <= config_.max_dt) {
            Point2D prev_velocity{
                (last.position.x - prev.position.x) / dt_prev,
                (last.position.y - prev.position.y) / dt_prev};
            prev_velocity = limitVectorNorm(prev_velocity, config_.max_speed);

            double dt_velocity = 0.5 * (dt_prev + dt_current);
            if (dt_velocity > 1e-6) {
                result.acceleration = Point2D{
                    (result.velocity.x - prev_velocity.x) / dt_velocity,
                    (result.velocity.y - prev_velocity.y) / dt_velocity};
                result.acceleration = limitVectorNorm(result.acceleration, config_.max_acceleration);
            }
        }
    }

    double predict_time = std::max(0.0, config_.predict_time);
    result.predicted_position.x += result.velocity.x * predict_time +
                                   0.5 * result.acceleration.x * predict_time * predict_time;
    result.predicted_position.y += result.velocity.y * predict_time +
                                   0.5 * result.acceleration.y * predict_time * predict_time;

    pushSample(measured_position, timestamp);
    result.prediction_applied = predict_time > 1e-6 && isFinitePosition(result.predicted_position);
    if (!result.prediction_applied) {
        result.predicted_position = measured_position;
    }

    return result;
}

} // namespace ball_motion_predictor
