from pathlib import Path
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.actions import ExecuteProcess, IncludeLaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    config = str(Path(get_package_share_directory('x5car_slam')) / 'config/slam_toolbox.yaml')
    upstream = str(Path(get_package_share_directory('slam_toolbox')) / 'launch/online_async_launch.py')

        # 1. 固定 TF：odom -> base_footprint
    odom_tf = Node(
        package='tf2_ros',
        executable='static_transform_publisher',
        name='fake_odom_tf',
        arguments=[
            '--x', '0', '--y', '0', '--z', '0',
            '--roll', '0', '--pitch', '0', '--yaw', '0',
            '--frame-id', 'odom',
            '--child-frame-id', 'base_footprint'
        ]
    )

    # 2. 固定 TF：base_footprint -> base_link
    base_tf = Node(
        package='tf2_ros',
        executable='static_transform_publisher',
        name='fake_base_tf',
        arguments=[
            '--x', '0', '--y', '0', '--z', '0',
            '--roll', '0', '--pitch', '0', '--yaw', '0',
            '--frame-id', 'base_footprint',
            '--child-frame-id', 'base_link'
        ]
    )

    # 3. 固定 TF：base_link -> laser
    laser_tf = Node(
        package='tf2_ros',
        executable='static_transform_publisher',
        name='fake_laser_tf',
        arguments=[
            '--x', '0', '--y', '0', '--z', '0',
            '--roll', '0', '--pitch', '0', '--yaw', '0',
            '--frame-id', 'base_link',
            '--child-frame-id', 'laser'
        ]
    )

    # 4. 以 10 Hz 发布固定的 /odom
    fake_odom = ExecuteProcess(
        cmd=[
            'ros2', 'topic', 'pub',
            '-r', '10',
            '/odom',
            'nav_msgs/msg/Odometry',
            (
                '{header: {frame_id: odom}, '
                'child_frame_id: base_footprint, '
                'pose: {pose: {orientation: {w: 1.0}}}}'
            )
        ],
        output='log'
    )
    
    return LaunchDescription([
        # odom_tf,
        # base_tf,
        # laser_tf,
        # fake_odom,
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        DeclareLaunchArgument('params_file', default_value=config),
        IncludeLaunchDescription(PythonLaunchDescriptionSource(upstream), launch_arguments={
            'use_sim_time': LaunchConfiguration('use_sim_time'),
            'slam_params_file': LaunchConfiguration('params_file'),
        }.items()),
    ])
