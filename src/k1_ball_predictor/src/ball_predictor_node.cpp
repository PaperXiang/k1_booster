#include "k1_ball_predictor/ball_motion_predictor.hpp"

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <vision_interface/msg/ball.hpp>

namespace {

k1_ball_predictor::Config loadConfig(rclcpp::Node &node) {
    k1_ball_predictor::Config config;
    config.enable = node.declare_parameter<bool>("enable", true);
    config.predict_time = node.declare_parameter<double>("predict_time", 0.25);
    config.min_dt = node.declare_parameter<double>("min_dt", 0.02);
    config.max_dt = node.declare_parameter<double>("max_dt", 0.20);
    config.max_history_gap = node.declare_parameter<double>("max_history_gap", 0.50);
    config.max_speed = node.declare_parameter<double>("max_speed", 4.0);
    config.max_acceleration = node.declare_parameter<double>("max_acceleration", 8.0);
    config.min_confidence = node.declare_parameter<double>("min_confidence", 40.0);
    config.confidence_full = node.declare_parameter<double>("confidence_full", 100.0);
    config.confidence_noise_gain = node.declare_parameter<double>("confidence_noise_gain", 2.0);
    config.enable_kalman = node.declare_parameter<bool>("enable_kalman", true);
    config.prefer_kalman_velocity = node.declare_parameter<bool>("prefer_kalman_velocity", true);
    config.process_noise_position = node.declare_parameter<double>("process_noise_position", 0.03);
    config.process_noise_velocity = node.declare_parameter<double>("process_noise_velocity", 0.60);
    config.measurement_noise = node.declare_parameter<double>("measurement_noise", 0.06);
    config.initial_covariance = node.declare_parameter<double>("initial_covariance", 1.0);
    config.lost_prediction_timeout = node.declare_parameter<double>("lost_prediction_timeout", 0.4);
    config.max_jump_distance = node.declare_parameter<double>("max_jump_distance", 1.2);
    config.max_jump_speed = node.declare_parameter<double>("max_jump_speed", 6.0);
    config.velocity_smoothing = node.declare_parameter<double>("velocity_smoothing", 0.5);
    config.acceleration_smoothing = node.declare_parameter<double>("acceleration_smoothing", 0.6);
    config.min_motion_speed = node.declare_parameter<double>("min_motion_speed", 0.05);
    config.acceleration_prediction_scale = node.declare_parameter<double>("acceleration_prediction_scale", 0.0);
    config.trajectory_step = node.declare_parameter<double>("trajectory_step", 0.1);
    config.trajectory_count = node.declare_parameter<int>("trajectory_count", 20);
    return config;
}

} // namespace

class BallPredictorNode : public rclcpp::Node {
public:
    BallPredictorNode()
        : Node("ball_predictor_node"), predictor_(loadConfig(*this)) {
        std::string input_topic = declare_parameter<std::string>("input_ball_topic", "/booster_vision/ball");
        std::string filtered_topic = declare_parameter<std::string>("filtered_ball_topic", "/k1_ball_predictor/ball_filtered");
        std::string predicted_topic = declare_parameter<std::string>("predicted_ball_topic", "/k1_ball_predictor/ball_predicted");

        filtered_pub_ = create_publisher<vision_interface::msg::Ball>(filtered_topic, rclcpp::QoS(10));
        predicted_pub_ = create_publisher<vision_interface::msg::Ball>(predicted_topic, rclcpp::QoS(10));
        ball_sub_ = create_subscription<vision_interface::msg::Ball>(
            input_topic,
            rclcpp::QoS(10),
            [this](vision_interface::msg::Ball::ConstSharedPtr msg) { ballCallback(msg); });

        RCLCPP_INFO(get_logger(), "Ball predictor node started, input=%s", input_topic.c_str());
    }

private:
    void ballCallback(const vision_interface::msg::Ball::ConstSharedPtr &msg) {
        k1_ball_predictor::Observation observation;
        observation.stamp = rclcpp::Time(msg->header.stamp);
        observation.position = k1_ball_predictor::Point2D{msg->x, msg->y};
        observation.confidence = msg->confidence;
        observation.reliable = msg->confidence >= predictor_.config().min_confidence;

        auto result = predictor_.update(observation);
        if (!result.valid) {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 1000, "Ball prediction invalid");
            return;
        }

        vision_interface::msg::Ball filtered;
        filtered.header = msg->header;
        filtered.x = result.filtered_position.x;
        filtered.y = result.filtered_position.y;
        filtered.confidence = msg->confidence;
        filtered_pub_->publish(filtered);

        vision_interface::msg::Ball predicted;
        predicted.header = msg->header;
        predicted.x = result.predicted_position.x;
        predicted.y = result.predicted_position.y;
        predicted.confidence = msg->confidence;
        predicted_pub_->publish(predicted);
    }

    k1_ball_predictor::BallMotionPredictor predictor_;
    rclcpp::Subscription<vision_interface::msg::Ball>::SharedPtr ball_sub_;
    rclcpp::Publisher<vision_interface::msg::Ball>::SharedPtr filtered_pub_;
    rclcpp::Publisher<vision_interface::msg::Ball>::SharedPtr predicted_pub_;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<BallPredictorNode>());
    rclcpp::shutdown();
    return 0;
}
