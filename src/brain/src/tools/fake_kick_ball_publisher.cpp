#include <chrono>
#include <algorithm>
#include <functional>
#include <memory>

#include "brain/msg/kick.hpp"
#include "rclcpp/rclcpp.hpp"

class FakeKickBallPublisher : public rclcpp::Node
{
public:
    FakeKickBallPublisher() : Node("fake_kick_ball_publisher")
    {
        pub_ = create_publisher<brain::msg::Kick>("/kick_ball", 10);

        declare_parameter<double>("x", 3.0);
        declare_parameter<double>("y", 0.0);
        declare_parameter<double>("dir", 0.0);
        declare_parameter<double>("goal_x", 6.0);
        declare_parameter<double>("goal_y", 0.0);
        declare_parameter<double>("robot_theta_to_field", 0.0);
        declare_parameter<double>("power", 6.0);
        declare_parameter<double>("rate_hz", 20.0);
        declare_parameter<bool>("oneshot", false);

        const double rate_hz = std::max(0.1, get_parameter("rate_hz").as_double());
        const auto period = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double>(1.0 / rate_hz));

        timer_ = create_wall_timer(period, std::bind(&FakeKickBallPublisher::tick, this));

        RCLCPP_WARN(
            get_logger(),
            "Publishing TEST /kick_ball messages only. Stop this node before running normal brain tests.");
    }

private:
    void tick()
    {
        brain::msg::Kick msg;
        msg.header.stamp = now();
        msg.header.frame_id = "base_link";
        msg.x = get_parameter("x").as_double();
        msg.y = get_parameter("y").as_double();
        msg.dir = get_parameter("dir").as_double();
        msg.goal_x = get_parameter("goal_x").as_double();
        msg.goal_y = get_parameter("goal_y").as_double();
        msg.robot_theta_to_field = get_parameter("robot_theta_to_field").as_double();
        msg.power = get_parameter("power").as_double();

        pub_->publish(msg);

        if (get_parameter("oneshot").as_bool()) {
            rclcpp::shutdown();
        }
    }

    rclcpp::Publisher<brain::msg::Kick>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<FakeKickBallPublisher>());
    rclcpp::shutdown();
    return 0;
}
