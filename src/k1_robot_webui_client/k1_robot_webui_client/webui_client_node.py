import json
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

from .api_client import WebuiApiClient, quote_robot_id


class WebuiClientNode(Node):
    def __init__(self):
        super().__init__("webui_client_node")

        self.declare_parameter("robot_id", "k1-robot")
        self.declare_parameter("server_base_url", "http://192.168.1.100:8000")
        self.declare_parameter("heartbeat_period_sec", 1.0)
        self.declare_parameter("telemetry_period_sec", 0.2)
        self.declare_parameter("request_timeout_sec", 0.5)

        self.robot_id = self.get_parameter("robot_id").value
        self.server_base_url = self.get_parameter("server_base_url").value
        heartbeat_period = float(self.get_parameter("heartbeat_period_sec").value)
        telemetry_period = float(self.get_parameter("telemetry_period_sec").value)
        request_timeout = float(self.get_parameter("request_timeout_sec").value)

        self.api = WebuiApiClient(self.server_base_url, request_timeout)
        self.robot_path_id = quote_robot_id(self.robot_id)
        self.latest_status = None
        self.latest_status_received_at = None
        self.last_error_log_time = 0.0

        self.create_subscription(String, "/brain/status_json", self.status_callback, 10)
        self.create_timer(max(0.1, heartbeat_period), self.send_heartbeat)
        self.create_timer(max(0.05, telemetry_period), self.send_telemetry)

        self.get_logger().info(
            f"Reporting WebUI telemetry for {self.robot_id} to {self.server_base_url}"
        )

    def status_callback(self, msg: String):
        try:
            self.latest_status = json.loads(msg.data)
        except json.JSONDecodeError:
            self.latest_status = {"raw": msg.data}
        self.latest_status_received_at = time.time()

    def warn_request_failure(self, action: str, error: str):
        now = time.time()
        if now - self.last_error_log_time > 5.0:
            self.get_logger().warn(f"WebUI {action} failed: {error}")
            self.last_error_log_time = now

    def send_heartbeat(self):
        payload = {
            "robot_id": self.robot_id,
            "client_time": time.time(),
            "has_status": self.latest_status is not None,
            "status_received_at": self.latest_status_received_at,
        }
        ok, error = self.api.post_json(f"/api/robots/{self.robot_path_id}/heartbeat", payload)
        if not ok:
            self.warn_request_failure("heartbeat", error)

    def send_telemetry(self):
        if self.latest_status is None:
            return
        payload = {
            "robot_id": self.robot_id,
            "client_time": time.time(),
            "status_received_at": self.latest_status_received_at,
            "status": self.latest_status,
        }
        ok, error = self.api.post_json(f"/api/robots/{self.robot_path_id}/telemetry", payload)
        if not ok:
            self.warn_request_failure("telemetry", error)


def main(args=None):
    rclpy.init(args=args)
    node = WebuiClientNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
