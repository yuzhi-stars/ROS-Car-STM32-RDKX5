"""hardware / mapping / navigation. EKF is deliberately NOT included."""
from pathlib import Path
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _setup(context):
    value = lambda name: LaunchConfiguration(name).perform(context)
    mode = value('mode')
    if mode not in ('hardware', 'mapping', 'navigation'):
        raise ValueError('mode must be hardware, mapping or navigation')
    lidar = value('enable_lidar').lower() == 'true'
    if mode != 'hardware' and not lidar:
        raise ValueError('mapping/navigation requires enable_lidar:=true or use separate launches with an existing /scan')
    def include(package, name, args):
        path = Path(get_package_share_directory(package)) / 'launch' / name
        return IncludeLaunchDescription(PythonLaunchDescriptionSource(str(path)), launch_arguments=args.items())
    common = {'use_sim_time': value('use_sim_time')}
    actions = [
        include('x5car_bringup', 'hardware.launch.py', dict(common,
            use_mock_hardware=value('use_mock_hardware'), hardware_file=value('hardware_file'),
            geometry_file=value('geometry_file'))),
        include('x5car_bringup', 'sensors.launch.py', {
            'enable_lidar': value('enable_lidar'), 'enable_camera': value('enable_camera'),
            'lidar_config': value('lidar_config'), 'camera_package': value('camera_package'),
            'camera_launch': value('camera_launch')}),
    ]
    if mode == 'mapping':
        actions.append(include('x5car_slam', 'slam.launch.py', common))
    if mode == 'navigation' or (mode == 'mapping' and value('with_navigation').lower() == 'true'):
        actions.append(include('x5car_navigation', 'navigation.launch.py', dict(common,
            localization='true' if mode == 'navigation' else 'false', map=value('map'),
            geometry_file=value('geometry_file'), use_depth=value('enable_camera'))))
    if value('rviz').lower() == 'true':
        package = 'x5car_description' if mode == 'hardware' else 'x5car_navigation'
        name = 'model.rviz' if mode == 'hardware' else 'navigation.rviz'
        path = Path(get_package_share_directory(package)) / 'rviz' / name
        actions.append(Node(package='rviz2', executable='rviz2', arguments=['-d', str(path)],
                            parameters=[{'use_sim_time': value('use_sim_time').lower() == 'true'}]))
    return actions


def generate_launch_description():
    bringup = Path(get_package_share_directory('x5car_bringup'))
    description = Path(get_package_share_directory('x5car_description'))
    defaults = {
        'mode': 'hardware', 'use_sim_time': 'false', 'use_mock_hardware': 'false',
        'enable_lidar': 'false', 'enable_camera': 'false', 'rviz': 'false',
        'with_navigation': 'false', 'map': '',
        'hardware_file': str(bringup / 'config/hardware.yaml'),
        'geometry_file': str(description / 'config/robot.yaml'),
        'lidar_config': str(bringup / 'config/lidar.yaml'),
        'camera_package': 'astra_camera', 'camera_launch': 'astra_pro.launch.xml'}
    return LaunchDescription([DeclareLaunchArgument(k, default_value=v) for k, v in defaults.items()] +
                             [OpaqueFunction(function=_setup)])
