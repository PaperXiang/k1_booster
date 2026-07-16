import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    config_file = os.path.join(
        get_package_share_directory("k1_robot_webui_client"),
        "config",
        "webui_client.yaml",
    )

    log_directory_argument = DeclareLaunchArgument(
        "log_directory",
        default_value=os.getcwd(),
        description="Directory containing brain.log, game_controller.log, and vision.log",
    )

    webui_client_node = Node(
        package="k1_robot_webui_client",
        executable="webui_client_node",
        name="webui_client_node",
        output="screen",
        parameters=[
            config_file,
            {"log_directory": LaunchConfiguration("log_directory")},
        ],
    )

    return LaunchDescription([
        log_directory_argument,
        webui_client_node,
    ])
