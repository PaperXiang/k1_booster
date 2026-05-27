#include "ball_motion_predictor/ball_motion_predictor.hpp"

#include <cmath>
#include <iostream>

namespace {

bool near(double lhs, double rhs, double tolerance) {
    return std::fabs(lhs - rhs) <= tolerance;
}

int expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    ball_motion_predictor::Config config;
    config.enable = true;
    config.predict_time = 0.1;
    config.min_dt = 0.01;
    config.max_dt = 0.2;
    config.max_history_gap = 0.5;
    config.max_speed = 10.0;
    config.max_acceleration = 10.0;

    ball_motion_predictor::BallMotionPredictor predictor(config);

    auto first = predictor.update({0.0, 0.0, 0.0}, 1.0, true);
    if (expect(!first.prediction_applied, "first sample should only seed history")) {
        return 1;
    }

    auto second = predictor.update({0.1, 0.0, 0.0}, 1.1, true);
    if (expect(second.prediction_applied, "second sample should apply velocity prediction")) {
        return 1;
    }
    if (expect(near(second.predicted_position.x, 0.2, 1e-6), "velocity prediction x mismatch")) {
        return 1;
    }

    auto third = predictor.update({0.25, 0.0, 0.0}, 1.2, true);
    if (expect(third.prediction_applied, "third sample should apply acceleration prediction")) {
        return 1;
    }
    if (expect(third.acceleration.x > 0.0, "third sample should estimate positive acceleration")) {
        return 1;
    }
    if (expect(third.predicted_position.x > 0.35, "acceleration prediction should be ahead of velocity-only value")) {
        return 1;
    }

    auto reset = predictor.update({0.3, 0.0, 0.0}, 2.0, true);
    if (expect(reset.history_reset, "long timestamp gap should reset history")) {
        return 1;
    }
    if (expect(!reset.prediction_applied, "reset sample should not be predicted")) {
        return 1;
    }

    return 0;
}
