"""Humble Nav2 servers with a single /odom input and holonomic DWB."""
from pathlib import Path
import copy
import os
import tempfile
import yaml
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction, RegisterEventHandler
from launch.event_handlers import OnShutdown
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _merge(base, overrides):
    for key, value in overrides.items():
        if isinstance(value, dict) and isinstance(base.get(key), dict):
            _merge(base[key], value)
        else:
            base[key] = copy.deepcopy(value)


def _cleanup(context, filename):
    if os.path.exists(filename):
        os.unlink(filename)
    return []


def _setup(context):
    value = lambda name: LaunchConfiguration(name).perform(context)
    nav2 = Path(get_package_share_directory('nav2_bringup'))
    params = yaml.safe_load((nav2 / 'params/nav2_params.yaml').read_text(encoding='utf-8'))
    overrides = yaml.safe_load(Path(value('params_file')).expanduser().read_text(encoding='utf-8'))
    _merge(params, overrides)
    simulated = value('use_sim_time').lower() == 'true'
    def set_time(tree):
        if isinstance(tree, dict):
            for key, child in tree.items():
                if key == 'ros__parameters':
                    child['use_sim_time'] = simulated
                else:
                    set_time(child)
    set_time(params)
    g = yaml.safe_load(Path(value('geometry_file')).expanduser().read_text(encoding='utf-8'))['geometry']
    hx = max(g['body_length'] / 2.0, g['half_wheelbase'] + g['wheel_radius'])
    hy = max(g['body_width'] / 2.0, g['half_track'] + g['wheel_width'] / 2.0)
    footprint = str([[hx, hy], [hx, -hy], [-hx, -hy], [-hx, hy]])
    for key in ('local_costmap', 'global_costmap'):
        config = params[key][key]['ros__parameters']
        config.pop('robot_radius', None)
        config['footprint'] = footprint
        config['footprint_padding'] = 0.02
        if value('use_depth').lower() == 'true':
            config['obstacle_layer']['observation_sources'] = 'scan depth_points'
    localize = value('localization').lower() == 'true'
    map_file = Path(value('map')).expanduser() if value('map') else None
    if localize:
        if map_file is None or not map_file.is_file():
            raise ValueError('AMCL navigation requires map:=/absolute/path/map.yaml')
        map_info = yaml.safe_load(map_file.read_text(encoding='utf-8'))
        image = Path(map_info['image']).expanduser()
        if not image.is_absolute():
            image = map_file.parent / image
        if not image.is_file():
            raise FileNotFoundError(image)
    with tempfile.NamedTemporaryFile(mode='w', prefix='x5car_nav2_', suffix='.yaml', delete=False) as output:
        yaml.safe_dump(params, output, sort_keys=False)
        generated = output.name
    actions = [RegisterEventHandler(OnShutdown(on_shutdown=[OpaqueFunction(
        function=_cleanup, args=[generated])]))]
    if localize:
        actions.append(IncludeLaunchDescription(PythonLaunchDescriptionSource(
            str(nav2 / 'launch/localization_launch.py')), launch_arguments={
                'map': str(map_file.resolve()), 'params_file': generated,
                'use_sim_time': value('use_sim_time'), 'autostart': 'true',
                'use_composition': 'False'}.items()))
    servers = [
        ('nav2_controller', 'controller_server'),
        ('nav2_smoother', 'smoother_server'),
        ('nav2_planner', 'planner_server'),
        ('nav2_behaviors', 'behavior_server'),
        ('nav2_bt_navigator', 'bt_navigator'),
        ('nav2_waypoint_follower', 'waypoint_follower'),
        ('nav2_velocity_smoother', 'velocity_smoother')]
    for package, name in servers:
        # Humble Nav2 的这些 cmd_vel 端点均为 geometry_msgs/msg/Twist。
        # remappings 只统一名称，不转换类型；不假设新版 stamped 参数在 Humble 可用。
        remaps = []
        if name in ('controller_server', 'behavior_server'):
            remaps = [('cmd_vel', '/cmd_vel_nav_raw')]
        if name == 'velocity_smoother':
            remaps = [('cmd_vel', '/cmd_vel_nav_raw'), ('cmd_vel_smoothed', '/cmd_vel_navigation')]
        actions.append(Node(package=package, executable=name, name=name, output='screen',
                            parameters=[generated], remappings=remaps))
    actions.append(Node(package='nav2_lifecycle_manager', executable='lifecycle_manager',
                        name='lifecycle_manager_navigation', output='screen', parameters=[{
                            'use_sim_time': simulated, 'autostart': True,
                            'node_names': [name for _, name in servers]}]))
    return actions


def generate_launch_description():
    config = Path(get_package_share_directory('x5car_navigation')) / 'config/nav2.yaml'
    geometry = Path(get_package_share_directory('x5car_description')) / 'config/robot.yaml'
    defaults = {'params_file': str(config), 'geometry_file': str(geometry),
                'use_sim_time': 'false', 'use_depth': 'false', 'localization': 'true', 'map': ''}
    return LaunchDescription([DeclareLaunchArgument(k, default_value=v) for k, v in defaults.items()] +
                             [OpaqueFunction(function=_setup)])
