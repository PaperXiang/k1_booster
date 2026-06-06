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

ball_kalman_filter::Config toKalmanConfig(const Config &config) {
    ball_kalman_filter::Config kalman_config;
    kalman_config.process_noise_position = config.kalman_process_noise_position;
    kalman_config.process_noise_velocity = config.kalman_process_noise_velocity;
    kalman_config.measurement_noise = config.kalman_measurement_noise;
    kalman_config.initial_covariance = config.kalman_initial_covariance;
    return kalman_config;
}

} // namespace

BallMotionPredictor::BallMotionPredictor(const Config &config)
    : config_(config), kalman_filter_(toKalmanConfig(config)) {}

void BallMotionPredictor::setConfig(const Config &config) {
    config_ = config;
    kalman_filter_.setConfig(toKalmanConfig(config_));
    reset();
}

const Config &BallMotionPredictor::config() const {
    return config_;
}

void BallMotionPredictor::reset() {
    history_.clear();
    velocity_history_.clear();
    kalman_filter_.reset();
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
    result.filtered_position = measured_position;
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

    Point3D position_for_prediction = measured_position;
    if (config_.enable_kalman_filter) {
        double kalman_dt = history_.empty() ? 0.0 : timestamp - history_.back().timestamp;
        bool valid_kalman_dt = kalman_dt >= config_.min_dt && kalman_dt <= config_.max_dt;
        auto kalman_result = kalman_filter_.update(
            ball_kalman_filter::Point2D{measured_position.x, measured_position.y},
            kalman_dt,
            valid_kalman_dt);
        position_for_prediction = Point3D{kalman_result.position.x, kalman_result.position.y, measured_position.z};
        result.filtered_position = position_for_prediction;
        result.filtered_velocity = limitVectorNorm(
            Point2D{kalman_result.velocity.x, kalman_result.velocity.y},
            config_.max_speed);
        result.kalman_initialized = kalman_result.initialized;
        result.predicted_position = position_for_prediction;
    }

    if (history_.empty()) {
        pushSample(position_for_prediction, timestamp);
        return result;
    }

    const auto &last = history_.back();
    double dt_current = timestamp - last.timestamp;
    if (dt_current < config_.min_dt || dt_current > config_.max_dt) {
        velocity_history_.clear();
        pushSample(position_for_prediction, timestamp);
        return result;
    }

    result.velocity = Point2D{
        (position_for_prediction.x - last.position.x) / dt_current,
        (position_for_prediction.y - last.position.y) / dt_current};
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

    pushSample(position_for_prediction, timestamp);
    pushVelocitySample(result.velocity, velocity_timestamp);
    result.prediction_applied = predict_time > 1e-6 && isFinitePosition(result.predicted_position);
    if (!result.prediction_applied) {
        result.predicted_position = measured_position;
    }

    return result;
}

} // namespace ball_motion_predictor
