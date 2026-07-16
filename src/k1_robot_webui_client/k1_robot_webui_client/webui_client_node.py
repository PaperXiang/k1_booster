import json
import os
import time
from collections import deque
from itertools import islice

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

from .api_client import WebuiApiClient, quote_robot_id
from .log_tailer import LogFileTailer


def resolve_log_path(log_directory: str, log_file_name: str) -> str:
    expanded_file_name = os.path.expanduser(log_file_name)
    if os.path.isabs(expanded_file_name):
        return expanded_file_name
    return os.path.abspath(
        os.path.join(os.path.expanduser(log_directory), expanded_file_name)
    )


class WebuiClientNode(Node):
    def __init__(self):
        super().__init__("webui_client_node")

        self.declare_parameter("robot_id", "k1-robot")
        self.declare_parameter("server_base_url", "http://192.168.1.100:8000")
        self.declare_parameter("heartbeat_period_sec", 1.0)
        self.declare_parameter("telemetry_period_sec", 0.2)
        self.declare_parameter("request_timeout_sec", 0.5)
        self.declare_parameter("log_directory", ".")
        self.declare_parameter("brain_log_file", "brain.log")
        self.declare_parameter("game_controller_log_file", "game_controller.log")
        self.declare_parameter("vision_log_file", "vision.log")
        self.declare_parameter("log_poll_period_sec", 0.5)
        self.declare_parameter("log_initial_line_count", 300)
        self.declare_parameter("log_batch_line_count", 200)
        self.declare_parameter("log_read_byte_limit", 65536)
        self.declare_parameter("log_pending_batch_limit", 120)

        self.robot_id = self.get_parameter("robot_id").value
        self.server_base_url = self.get_parameter("server_base_url").value
        heartbeat_period = float(self.get_parameter("heartbeat_period_sec").value)
        telemetry_period = float(self.get_parameter("telemetry_period_sec").value)
        request_timeout = float(self.get_parameter("request_timeout_sec").value)
        log_directory = str(self.get_parameter("log_directory").value)
        log_poll_period = float(self.get_parameter("log_poll_period_sec").value)
        log_initial_line_count = int(self.get_parameter("log_initial_line_count").value)
        log_batch_line_count = int(self.get_parameter("log_batch_line_count").value)
        log_read_byte_limit = int(self.get_parameter("log_read_byte_limit").value)
        self.log_pending_batch_limit = max(
            12,
            int(self.get_parameter("log_pending_batch_limit").value),
        )

        self.api = WebuiApiClient(self.server_base_url, request_timeout)
        self.robot_path_id = quote_robot_id(self.robot_id)
        self.latest_status = None
        self.latest_status_received_at = None
        self.last_error_log_time = 0.0
        self.pending_log_batches: deque[dict] = deque()
        self.log_sources_requiring_reset: set[str] = set()

        log_file_names = {
            "brain": str(self.get_parameter("brain_log_file").value),
            "game_controller": str(
                self.get_parameter("game_controller_log_file").value
            ),
            "vision": str(self.get_parameter("vision_log_file").value),
        }
        log_paths = {
            source: resolve_log_path(log_directory, file_name)
            for source, file_name in log_file_names.items()
        }
        self.log_tailers = [
            LogFileTailer(
                source=source,
                path=path,
                initial_line_limit=log_initial_line_count,
                batch_line_limit=log_batch_line_count,
                read_byte_limit=log_read_byte_limit,
            )
            for source, path in log_paths.items()
        ]

        self.create_subscription(String, "/brain/status_json", self.status_callback, 10)
        self.create_timer(max(0.1, heartbeat_period), self.send_heartbeat)
        self.create_timer(max(0.05, telemetry_period), self.send_telemetry)
        self.create_timer(max(0.1, log_poll_period), self.collect_and_send_logs)

        self.get_logger().info(
            f"Reporting WebUI telemetry for {self.robot_id} to {self.server_base_url}"
        )
        self.get_logger().info(
            "Streaming WebUI logs from "
            + ", ".join(f"{source}={path}" for source, path in log_paths.items())
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

    def collect_and_send_logs(self):
        for log_tailer in self.log_tailers:
            batch = log_tailer.read_batch()
            if batch is None:
                continue
            self._queue_log_batch(
                {
                    "source": batch.source,
                    "lines": batch.lines,
                    "reset": batch.reset,
                }
            )

        if not self.pending_log_batches:
            return

        batches_to_upload = list(islice(self.pending_log_batches, 0, 12))
        payload = {
            "robot_id": self.robot_id,
            "client_time": time.time(),
            "batches": batches_to_upload,
        }
        ok, error = self.api.post_json(
            f"/api/robots/{self.robot_path_id}/logs",
            payload,
        )
        if not ok:
            self.warn_request_failure("log upload", error)
            return

        for _ in range(len(batches_to_upload)):
            self.pending_log_batches.popleft()

    def _queue_log_batch(self, batch: dict):
        if len(self.pending_log_batches) >= self.log_pending_batch_limit:
            dropped_batch = self.pending_log_batches.popleft()
            self._mark_source_for_reset(str(dropped_batch["source"]))

        source = str(batch["source"])
        if source in self.log_sources_requiring_reset:
            batch["reset"] = True
            batch["lines"] = [
                "[webui] older lines were dropped because the upload queue was full",
                *batch["lines"],
            ]
            self.log_sources_requiring_reset.discard(source)

        self.pending_log_batches.append(batch)

    def _mark_source_for_reset(self, source: str):
        for pending_batch in self.pending_log_batches:
            if pending_batch["source"] != source:
                continue
            pending_batch["reset"] = True
            pending_batch["lines"] = [
                "[webui] older lines were dropped because the upload queue was full",
                *pending_batch["lines"],
            ]
            return

        self.log_sources_requiring_reset.add(source)


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
