#include "ball_kalman_filter/ball_kalman_filter.hpp"

#include <cmath>
#include <iostream>

namespace {

int expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    ball_kalman_filter::Config config;
    ball_kalman_filter::BallKalmanFilter filter(config);

    auto first = filter.update({0.0, 0.0}, 0.0, false);
    if (expect(first.initialized, "filter should initialize on first measurement")) {
        return 1;
    }

    auto second = filter.update({0.1, 0.0}, 0.1, true);
    if (expect(second.position.x > 0.0 && second.position.x < 0.11,
               "filter should track a nearby second measurement")) {
        return 1;
    }

    ball_kalman_filter::Config robust_config;
    robust_config.measurement_noise = 1.0;
    ball_kalman_filter::BallKalmanFilter robust_filter(robust_config);
    robust_filter.update({0.0, 0.0}, 0.0, false);
    robust_filter.update({0.1, 0.0}, 0.1, true);
    auto noisy = robust_filter.update({1.0, 0.0}, 0.1, true);
    if (expect(noisy.position.x < 0.95,
               "filter should not fully follow a noisy measurement")) {
        return 1;
    }
    if (expect(std::fabs(noisy.position.x - 0.2) < std::fabs(1.0 - 0.2),
               "filter output should stay closer to nominal motion than the outlier")) {
        return 1;
    }

    robust_filter.reset();
    if (expect(!robust_filter.initialized(), "reset should clear initialization")) {
        return 1;
    }

    return 0;
}
