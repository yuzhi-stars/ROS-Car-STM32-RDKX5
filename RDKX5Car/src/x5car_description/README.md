# x5car_description

维护机器人几何、四轮关节、IMU/雷达/相机安装外参以及 ros2_control 声明。

| 文件 | 用途 |
|---|---|
| `config/robot.yaml` | 轮径、轴距/轮距的一半、车体尺寸、质量及传感器外参，全部 SI 单位 |
| `urdf/x5car.urdf.xacro` | 模型与真实/GenericSystem mock 硬件选择 |
| `launch/display.launch.py` | 仅显示模型，不打开串口、不启动 controller_manager |
| `rviz/model.rviz` | 模型显示配置，固定坐标 base_link |

已确认完整前后轮中心距 16.7 cm、左右轮中心距 18.8 cm，配置分别存一半：0.0835 m、0.094 m。
已确认轮半径 40 mm，配置 wheel_radius=0.040 m（直径 80 mm）。
轮宽、车体尺寸/质量和安装外参尚未确认，文件中的其余数值仍是示例。
bringup 自动使用相同文件计算控制器轮半径及 lx+ly=0.1775 m；Nav2 自动计算 footprint。
没有真实滚子网格，圆柱只用于可视化，不能用于物理麦轮接触仿真。

base_link 位于底盘中心地面投影；固定 TF 为 base_link 到 body_link、imu_link、laser、camera_link。
轮子的关节 TF 由 robot_state_publisher 根据 joint_states 发布；+Y 轮轴配合正轮速表示向前。
相机内部光学 TF 留给厂商驱动，URDF 不重复创建 camera_depth_optical_frame 等。
本包不发布 map->odom 或 odom->base_link。它需要同工作空间 bringup 提供 hardware.yaml。

仅显示模型：`ros2 launch x5car_description display.launch.py`。
需要 GUI 关节调节时加 `gui:=true`。不要与真实硬件的 joint_state_broadcaster 同时启动该显示启动文件。

## robot.yaml 字段怎么量

当前 Xacro 中的 `base_link` 原点位于四轮中心矩形的几何中心在地面的投影。
车体坐标 x 向前、y 向左、z 向上。长度统一 m，角度统一 rad，质量统一 kg。
模型假定四轮半径和轮宽相同、四轮位置为对称矩形。

| geometry 字段 | 物理含义 / 测量方法 | 代码中用途 |
|---|---|---|
| `wheel_radius` | 轮轴中心到轮子滚动外缘的半径；可测承载时轴心到地面的高度。不是直径，也不是电机轴或麦轮小滚子的半径 | 控制器速度换算、里程计、轮轴 z 高度、圆柱模型、外廓 |
| `wheel_width` | 单个轮子沿轮轴方向的总宽度。从轮子左侧面量到右侧面；当前圆柱模型中正是 `cylinder length`，即几何上圆柱的“高” | 圆柱模型、惯量、左右外廓；不直接进入麦轮运动学公式 |
| `half_wheelbase` | 前后轮中心距的一半；前轮 x=+该值、后轮 x=-该值。16.7 cm / 2 = 0.0835 m | 轮关节位置、控制器 lx+ly、前后外廓 |
| `half_track` | 左右轮中心距的一半；左轮 y=+该值、右轮 y=-该值。18.8 cm / 2 = 0.094 m | 轮关节位置、控制器 lx+ly、左右外廓 |
| `body_length` | 简化车体箱沿 x 的前后长度；不包含另建圆柱的轮子 | 车体模型、惯量、导航外廓 |
| `body_width` | 简化车体箱沿 y 的左右宽度；不包含另建圆柱的轮子 | 车体模型、惯量、导航外廓 |
| `body_height` | 车体箱沿 z 的上下厚度；不是整车离地总高 | 模型、惯量 |
| `body_center_z` | 地面到车体箱几何中心的高度；箱底=`body_center_z-body_height/2`，箱顶=`body_center_z+body_height/2` | 固定 TF `base_link -> body_link` |
| `body_mass` | 简化车体及归并载荷的质量，不含另建模的四个车轮 | URDF 质量/惯量，不决定真实电机 PID 或限速 |
| `wheel_mass` | 单个车轮质量，四轮分别使用该值 | 各车轮 URDF 质量/惯量 |
| `wheel_velocity_limit` | 单轮关节速度上限，单位 rad/s；当前只是 URDF `<limit velocity>` 元数据 | 当前真实硬件不按它限速，实际命令上限在 hardware.yaml |
| `wheel_effort_limit` | 单轮关节力矩上限，单位 N·m；不是 PWM、电流或 PID 参数 | 当前只是 URDF `<limit effort>` 元数据 |

轮宽虽然在圆柱几何中叫“高”，装车后圆柱轴转到车体 y 方向，所以它表现为左右宽度。
轮子最上缘到最下缘的距离是直径 `2*wheel_radius`，不要填入 wheel_width。
当前 Xacro 用几何圆柱简化麦轮，不模拟每个滚子。

| mounts 下的项 | 最终 link / 参照 |
|---|---|
| `imu` | `imu_link`；原点应与 IMU 数据所用传感器坐标系对应 |
| `laser` | `laser`；原点应与激光驱动发布的扫描坐标系对应 |
| `camera` | `camera_link`；原点/方向按相机驱动定义，光学坐标系由驱动继续发布 |

三项都包含相同六个字段：`x/y/z` 为目标 link 原点相对于 base_link 的位置，单位 m；
`roll/pitch/yaw` 为固定安装方向，单位 rad，遵循右手规则绕 x/y/z，URDF 旋转矩阵为 Rz(yaw) Ry(pitch) Rx(roll)。
例如 z 表示离地高度，不能直接填写“高出顶板多少”；绕 z 的 90 度应填约 1.570796。
这些是安装外参，IMU 自己输出的动态姿态四元数与它们不是同一项数据。

## 参数传到哪里

`x5car_bringup/launch/hardware.launch.py` 将 wheel_radius 和 half_wheelbase+half_track
分别写入麦轮控制器的 wheels_radius 与 sum_of_robot_center_projection_on_X_Y_axis。
URDF 使用同一份几何和安装外参；不需要在 controllers.yaml 另抄一套轴距/轮距。

Nav2 从同一文件取车体与车轮的最大平面包络：
`hx=max(body_length/2, half_wheelbase+wheel_radius)`，
`hy=max(body_width/2, half_track+wheel_width/2)`。
因此轮中心距不能直接当作整车长宽。箱体以 x/y=0 为中心；偏置凸出物需要用足够大的居中外廓包住，
或后续单独细化 footprint。没有实测车体尺寸前，不据此缩小避障外廓。

MCU 串口名、最大允许轮轴速度和通信超时在 `x5car_bringup/config/hardware.yaml`，不在本文件。
已测试 2 rad/s 仅是本次目标速度，不是最大允许速度。
