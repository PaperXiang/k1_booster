from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    config_file = os.path.join(
        get_package_share_directory('k1_ball_predictor'),
        'config',
        'ball_prediction.yaml'
    )

    return LaunchDescription([
        Node(
            package='k1_ball_predictor',
            executable='ball_predictor_node',
            name='ball_predictor_node',
            output='screen',
            parameters=[config_file]
        )
    ])
