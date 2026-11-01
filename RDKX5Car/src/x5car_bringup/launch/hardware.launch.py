"""Controller manager + robot model. No EKF is started here."""
from pathlib import Path
import math
import os
import tempfile
import yaml
import xacro
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, EmitEvent, OpaqueFunction, RegisterEventHandler
from launch.event_handlers import OnProcessExit, OnShutdown
from launch.events import Shutdown
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _cleanup(context, path):
    if os.path.exists(path):
        os.unlink(path)
    return []


def _setup(context):
    value = lambda name: LaunchConfiguration(name).perform(context)
    bringup = Path(get_package_share_directory('x5car_bringup'))
    description = Path(get_package_share_directory('x5car_description'))
    geometry = Path(value('geometry_file')).expanduser()
    hardware = Path(value('hardware_file')).expanduser()
    geometry_data = yaml.safe_load(geometry.read_text(encoding='utf-8'))['geometry']
    if any(not isinstance(v, (int, float)) or not math.isfinite(v) or v <= 0
           for v in geometry_data.values()):
        raise ValueError('robot.yaml geometry must contain positive finite measurements')
    mock = value('use_mock_hardware').lower()
    if mock not in ('true', 'false'):
        raise ValueError('use_mock_hardware must be true or false')
    hw = yaml.safe_load(hardware.read_text(encoding='utf-8'))
    if mock == 'false' and (float(hw['max_wheel_speed_rad_s']) <= 0 or
                            'REPLACE' in str(hw['serial_port'])):
        raise ValueError('Set the measured motor limit and real MCU serial_port in hardware.yaml')
    robot_description = xacro.process_file(str(description / 'urdf/x5car.urdf.xacro'), mappings={
        'geometry_file': str(geometry), 'hardware_file': str(hardware),
        'use_mock_hardware': mock}).toxml()
    controllers = yaml.safe_load((bringup / 'config/controllers.yaml').read_text(encoding='utf-8'))
    controller = controllers['mecanum_controller']['ros__parameters']
    # Humble 的 Nav2/smoother/twist_mux 速度链路是 Twist。
    # reference_unstamped 只能配 use_stamped_vel=false；remap 不做类型转换。
    if controller.get('use_stamped_vel') is not False:
        raise ValueError('This command graph uses geometry_msgs/msg/Twist: set '
                         'mecanum_controller.use_stamped_vel=false. A remap cannot '
                         'convert Twist to TwistStamped.')
    controller['kinematics'] = {
        'base_frame_offset': {'x': 0.0, 'y': 0.0, 'theta': 0.0},
        'wheels_radius': float(geometry_data['wheel_radius']),
        'sum_of_robot_center_projection_on_X_Y_axis': float(
            geometry_data['half_wheelbase'] + geometry_data['half_track'])}
    use_sim_time = value('use_sim_time').lower() == 'true'
    for config in controllers.values():
        config['ros__parameters']['use_sim_time'] = use_sim_time
    with tempfile.NamedTemporaryFile(mode='w', prefix='x5car_controllers_', suffix='.yaml',
                                     delete=False) as output:
        yaml.safe_dump(controllers, output, sort_keys=False)
        generated = output.name
    manager = Node(package='controller_manager', executable='ros2_control_node',
                   output='screen', parameters=[{'robot_description': robot_description}, generated],
                   remappings=[
                       # geometry_msgs/msg/Twist；不能改接 ~/reference（TwistStamped）。
                       ('/mecanum_controller/reference_unstamped', '/cmd_vel'),
                       ('/mecanum_controller/odometry', value('odom_topic')),
                       # Humble 麦轮控制器的 TF 默认发布到私有 tf_odometry，必须接到 /tf。
                       ('/mecanum_controller/tf_odometry', '/tf'),
                       ('/imu_sensor_broadcaster/imu', '/imu/data')])
    rsp = Node(package='robot_state_publisher', executable='robot_state_publisher',
               parameters=[{'robot_description': robot_description, 'use_sim_time': use_sim_time}])
    def spawner(name):
        return Node(package='controller_manager', executable='spawner', output='screen',
                    arguments=[name, '--controller-manager', '/controller_manager',
                               '--controller-manager-timeout', '60'])
    joint = spawner('joint_state_broadcaster')
    imu = spawner('imu_sensor_broadcaster')
    mecanum = spawner('mecanum_controller')
    def next_or_stop(next_action):
        def callback(event, launch_context):
            if event.returncode != 0:
                return [EmitEvent(event=Shutdown(reason='Controller spawner failed'))]
            return [] if next_action is None else [next_action]
        return callback
    return [
        RegisterEventHandler(OnShutdown(on_shutdown=[OpaqueFunction(function=_cleanup, args=[generated])])),
        RegisterEventHandler(OnProcessExit(target_action=manager, on_exit=[
            EmitEvent(event=Shutdown(reason='controller_manager exited'))])),
        RegisterEventHandler(OnProcessExit(target_action=joint, on_exit=next_or_stop(imu))),
        RegisterEventHandler(OnProcessExit(target_action=imu, on_exit=next_or_stop(mecanum))),
        RegisterEventHandler(OnProcessExit(target_action=mecanum, on_exit=next_or_stop(None))),
        rsp, manager, joint,
        Node(package='twist_mux', executable='twist_mux', name='twist_mux', output='screen',
             parameters=[str(bringup / 'config/twist_mux.yaml'), {'use_sim_time': use_sim_time}],
             remappings=[('cmd_vel_out', '/cmd_vel')]),
    ]


def generate_launch_description():
    bringup = Path(get_package_share_directory('x5car_bringup'))
    description = Path(get_package_share_directory('x5car_description'))
    return LaunchDescription([
        DeclareLaunchArgument('hardware_file', default_value=str(bringup / 'config/hardware.yaml')),
        DeclareLaunchArgument('geometry_file', default_value=str(description / 'config/robot.yaml')),
        DeclareLaunchArgument('use_mock_hardware', default_value='false'),
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        DeclareLaunchArgument('odom_topic', default_value='/odom'),
        OpaqueFunction(function=_setup),
    ])
