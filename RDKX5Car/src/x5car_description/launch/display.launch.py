"""Model display only, without controller_manager or serial access."""
from pathlib import Path
import xacro
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _setup(context):
    description = Path(get_package_share_directory('x5car_description'))
    bringup = Path(get_package_share_directory('x5car_bringup'))
    robot = xacro.process_file(str(description / 'urdf/x5car.urdf.xacro'), mappings={
        'hardware_file': str(bringup / 'config/hardware.yaml'), 'use_mock_hardware': 'true'}).toxml()
    gui = LaunchConfiguration('gui').perform(context).lower() == 'true'
    actions = [
        Node(package='robot_state_publisher', executable='robot_state_publisher',
             parameters=[{'robot_description': robot}]),
        Node(package='joint_state_publisher_gui' if gui else 'joint_state_publisher',
             executable='joint_state_publisher_gui' if gui else 'joint_state_publisher')]
    if LaunchConfiguration('rviz').perform(context).lower() == 'true':
        actions.append(Node(package='rviz2', executable='rviz2',
                            arguments=['-d', str(description / 'rviz/model.rviz')]))
    return actions


def generate_launch_description():
    return LaunchDescription([DeclareLaunchArgument('gui', default_value='false'),
                              DeclareLaunchArgument('rviz', default_value='true'),
                              OpaqueFunction(function=_setup)])
