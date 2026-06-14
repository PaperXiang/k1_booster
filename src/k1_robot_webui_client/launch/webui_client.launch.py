import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config_file = os.path.join(
        get_package_share_directory("k1_robot_webui_client"),
        "config",
        "webui_client.yaml",
    )

    return LaunchDescription([
        Node(
            package="k1_robot_webui_client",
            executable="webui_client_node",
            name="webui_client_node",
            output="screen",
            parameters=[config_file],
        )
    ])
