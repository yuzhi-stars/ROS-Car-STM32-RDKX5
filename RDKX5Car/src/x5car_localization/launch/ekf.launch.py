from pathlib import Path
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

def generate_launch_description():
    config = str(Path(get_package_share_directory('x5car_localization')) / 'config/ekf.yaml')
    return LaunchDescription([
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        Node(package='robot_localization', executable='ekf_node', name='ekf_filter_node',
             output='screen', remappings=[('odometry/filtered', '/odom')], parameters=[config, {'use_sim_time': ParameterValue(
                 LaunchConfiguration('use_sim_time'), value_type=bool)}]),
    ])
