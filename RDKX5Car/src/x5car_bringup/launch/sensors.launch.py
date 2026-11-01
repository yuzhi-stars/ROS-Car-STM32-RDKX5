"""Optional vendor drivers. They never open the motor/IMU MCU UART."""
from pathlib import Path
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction
from launch.launch_description_sources import AnyLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _setup(context):
    value = lambda name: LaunchConfiguration(name).perform(context)
    actions = []
    if value('enable_lidar').lower() == 'true':
        actions.append(Node(package='lslidar_driver', executable='lslidar_driver_node',
                            name='lslidar_driver_node', namespace='x10', output='screen',
                            parameters=[value('lidar_config')]))
    if value('enable_camera').lower() == 'true':
        source = Path(get_package_share_directory(value('camera_package'))) / 'launch' / value('camera_launch')
        if not source.is_file():
            raise FileNotFoundError(source)
        actions.append(IncludeLaunchDescription(AnyLaunchDescriptionSource(str(source)),
            launch_arguments={'camera_name': 'camera', 'publish_tf': 'true', 'tf_publish_rate': '0.0',
                              'enable_point_cloud': 'true', 'enable_colored_point_cloud': 'false'}.items()))
    return actions


def generate_launch_description():
    bringup = Path(get_package_share_directory('x5car_bringup'))
    return LaunchDescription([
        DeclareLaunchArgument('enable_lidar', default_value='false'),
        DeclareLaunchArgument('enable_camera', default_value='false'),
        DeclareLaunchArgument('lidar_config', default_value=str(bringup / 'config/lidar.yaml')),
        DeclareLaunchArgument('camera_package', default_value='astra_camera'),
        DeclareLaunchArgument('camera_launch', default_value='astra_pro.launch.xml'),
        OpaqueFunction(function=_setup),
    ])
