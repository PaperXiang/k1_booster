#include "k1_ball_predictor/ball_motion_predictor.hpp"

#include <algorithm>
#include <cmath>

namespace k1_ball_predictor {
namespace {

bool isFinite(const Point2D &point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

double norm(const Point2D &point) {
    return std::hypot(point.x, point.y);
}

Point2D limitNorm(Point2D value, double max_norm) {
    if (max_norm <= 0.0) {
        return value;
    }
    double value_norm = norm(value);
    if (value_norm <= max_norm || value_norm <= 1e-9) {
        return value;
    }
    double scale = max_norm / value_norm;
    return Point2D{value.x * scale, value.y * scale};
}

} // namespace

BallMotionPredictor::BallMotionPredictor(const Config &config) : config_(config) {
    reset();
}

void BallMotionPredictor::setConfig(const Config &config) {
    config_ = config;
    reset();
}

const Config &BallMotionPredictor::config() const {
    return config_;
}

void BallMotionPredictor::reset() {
    kalman_ = KalmanState{};
    has_last_sample_ = false;
    has_last_velocity_ = false;
    last_position_ = Point2D{};
    last_velocity_ = Point2D{};
}

void BallMotionPredictor::initializeKalman(const Point2D &position) {
    kalman_ = KalmanState{};
    kalman_.x = position.x;
    kalman_.y = position.y;
    double initial_covariance = std::max(1e-9, config_.initial_covariance);
    for (int i = 0; i < 4; ++i) {
        kalman_.covariance[i][i] = initial_covariance;
    }
    kalman_.initialized = true;
}

void BallMotionPredictor::predictKalman(double dt) {
    if (!kalman_.initialized || dt <= 1e-6) {
        return;
    }

    kalman_.x += kalman_.vx * dt;
    kalman_.y += kalman_.vy * dt;

    double transition[4][4] = {
        {1.0, 0.0, dt, 0.0},
        {0.0, 1.0, 0.0, dt},
        {0.0, 0.0, 1.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
    };
    double temp[4][4] = {};
    double predicted[4][4] = {};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            for (int k = 0; k < 4; ++k) {
                temp[r][c] += transition[r][k] * kalman_.covariance[k][c];
            }
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            for (int k = 0; k < 4; ++k) {
                predicted[r][c] += temp[r][k] * transition[c][k];
            }
        }
    }

    predicted[0][0] += std::max(0.0, config_.process_noise_position) * dt * dt;
    predicted[1][1] += std::max(0.0, config_.process_noise_position) * dt * dt;
    predicted[2][2] += std::max(0.0, config_.process_noise_velocity) * dt;
    predicted[3][3] += std::max(0.0, config_.process_noise_velocity) * dt;

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            kalman_.covariance[r][c] = predicted[r][c];
        }
    }
}

void BallMotionPredictor::correctKalman(const Point2D &position) {
    if (!kalman_.initialized) {
        initializeKalman(position);
        return;
    }

    double measurement_noise = std::max(1e-9, config_.measurement_noise);
    double s00 = kalman_.covariance[0][0] + measurement_noise;
    double s01 = kalman_.covariance[0][1];
    double s10 = kalman_.covariance[1][0];
    double s11 = kalman_.covariance[1][1] + measurement_noise;
    double det = s00 * s11 - s01 * s10;
    if (std::fabs(det) <= 1e-12) {
        return;
    }

    double inv_s00 = s11 / det;
    double inv_s01 = -s01 / det;
    double inv_s10 = -s10 / det;
    double inv_s11 = s00 / det;
    double residual_x = position.x - kalman_.x;
    double residual_y = position.y - kalman_.y;
    double gain[4][2] = {};
    double state[4] = {kalman_.x, kalman_.y, kalman_.vx, kalman_.vy};

    for (int r = 0; r < 4; ++r) {
        double p0 = kalman_.covariance[r][0];
        double p1 = kalman_.covariance[r][1];
        gain[r][0] = p0 * inv_s00 + p1 * inv_s10;
        gain[r][1] = p0 * inv_s01 + p1 * inv_s11;
        state[r] += gain[r][0] * residual_x + gain[r][1] * residual_y;
    }

    double updated[4][4] = {};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double factor = (r == c ? 1.0 : 0.0);
            if (c == 0) {
                factor -= gain[r][0];
            } else if (c == 1) {
                factor -= gain[r][1];
            }
            for (int k = 0; k < 4; ++k) {
                updated[r][k] += factor * kalman_.covariance[c][k];
            }
        }
    }

    kalman_.x = state[0];
    kalman_.y = state[1];
    kalman_.vx = state[2];
    kalman_.vy = state[3];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            kalman_.covariance[r][c] = updated[r][c];
        }
    }
}

bool BallMotionPredictor::isOutlier(const Observation &observation, double dt) const {
    if (!has_last_sample_) {
        return false;
    }
    Point2D delta{observation.position.x - last_position_.x, observation.position.y - last_position_.y};
    double distance = norm(delta);
    if (config_.max_jump_distance > 0.0 && distance > config_.max_jump_distance) {
        return true;
    }
    if (dt >= config_.min_dt && config_.max_jump_speed > 0.0 && distance / dt > config_.max_jump_speed) {
        return true;
    }
    return false;
}

std::vector<Point2D> BallMotionPredictor::buildTrajectory(const Point2D &start,
                                                          const Point2D &velocity,
                                                          const Point2D &acceleration) const {
    std::vector<Point2D> trajectory;
    if (config_.trajectory_count <= 0 || config_.trajectory_step <= 0.0) {
        return trajectory;
    }
    trajectory.reserve(static_cast<std::size_t>(config_.trajectory_count));
    for (int i = 1; i <= config_.trajectory_count; ++i) {
        double t = config_.trajectory_step * i;
        trajectory.push_back(Point2D{
            start.x + velocity.x * t + 0.5 * acceleration.x * t * t,
            start.y + velocity.y * t + 0.5 * acceleration.y * t * t,
        });
    }
    return trajectory;
}

Result BallMotionPredictor::predictOnly(const rclcpp::Time &stamp) {
    Result result;
    result.stamp = stamp;
    result.predicted_only = true;
    if (!has_last_sample_) {
        return result;
    }

    double dt = (stamp - last_stamp_).seconds();
    if (dt < 0.0 || dt > config_.lost_prediction_timeout) {
        result.history_reset = true;
        reset();
        return result;
    }

    result.filtered_position = last_position_;
    result.velocity = last_velocity_;
    result.predicted_position = Point2D{last_position_.x + last_velocity_.x * dt,
                                        last_position_.y + last_velocity_.y * dt};
    result.trajectory = buildTrajectory(result.predicted_position, result.velocity, result.acceleration);
    result.valid = true;
    return result;
}

Result BallMotionPredictor::update(const Observation &observation) {
    Result result;
    result.stamp = observation.stamp;
    result.raw_position = observation.position;
    result.filtered_position = observation.position;
    result.predicted_position = observation.position;

    if (!config_.enable) {
        result.valid = observation.reliable && isFinite(observation.position);
        return result;
    }
    if (!isFinite(observation.position)) {
        result.history_reset = true;
        reset();
        return result;
    }
    if (!observation.reliable) {
        return predictOnly(observation.stamp);
    }

    double dt = has_last_sample_ ? (observation.stamp - last_stamp_).seconds() : 0.0;
    if (has_last_sample_ && (dt < 0.0 || dt > config_.max_history_gap)) {
        result.history_reset = true;
        reset();
        dt = 0.0;
    }
    if (isOutlier(observation, dt)) {
        result = predictOnly(observation.stamp);
        result.raw_position = observation.position;
        result.outlier_rejected = true;
        return result;
    }

    bool valid_dt = has_last_sample_ && dt >= config_.min_dt && dt <= config_.max_dt;
    Point2D filtered = observation.position;
    if (config_.enable_kalman) {
        if (!kalman_.initialized) {
            initializeKalman(observation.position);
        } else if (valid_dt) {
            predictKalman(dt);
        }
        correctKalman(observation.position);
        filtered = Point2D{kalman_.x, kalman_.y};
        result.velocity = limitNorm(Point2D{kalman_.vx, kalman_.vy}, config_.max_speed);
    }
    result.filtered_position = filtered;

    if (valid_dt) {
        Point2D velocity{(filtered.x - last_position_.x) / dt, (filtered.y - last_position_.y) / dt};
        velocity = limitNorm(velocity, config_.max_speed);
        result.velocity = velocity;
        if (has_last_velocity_) {
            Point2D acceleration{(velocity.x - last_velocity_.x) / dt, (velocity.y - last_velocity_.y) / dt};
            result.acceleration = limitNorm(acceleration, config_.max_acceleration);
        }
        last_velocity_ = velocity;
        has_last_velocity_ = true;
    }

    double predict_time = std::max(0.0, config_.predict_time);
    result.predicted_position = Point2D{
        filtered.x + result.velocity.x * predict_time + 0.5 * result.acceleration.x * predict_time * predict_time,
        filtered.y + result.velocity.y * predict_time + 0.5 * result.acceleration.y * predict_time * predict_time,
    };
    result.trajectory = buildTrajectory(filtered, result.velocity, result.acceleration);
    result.valid = isFinite(result.predicted_position);

    has_last_sample_ = true;
    last_stamp_ = observation.stamp;
    last_position_ = filtered;
    return result;
}

} // namespace k1_ball_predictor
