#ifndef X5CAR_PROTOCOL_HPP_
#define X5CAR_PROTOCOL_HPP_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

/**
 * @file x5car_protocol.hpp
 * @brief RDK X5 <-> 下位机最小 UART 协议，C++17，无 ROS/串口库依赖。
 *
 * ================= 下位机开发者：以下为本版本确定的线协议 =================
 *
 * 1. 串口：115200 baud，8N1，全双工。所有数组轮序：FL、FR、RL、RR。
 *    正轮速表示该轮向车体前方滚动；速度单位是轮轴 rad/s，已考虑减速比。
 *    三种帧均为固定长度，没有长度、时间戳、序号、帧尾或字符串结束符。
 *
 * 2. 通用布局：
 *      AA 55 | TYPE(1字节) | PAYLOAD(固定长度) | CRC_LO CRC_HI
 *    TYPE=0x03：上位机 -> 下位机，四轮目标速度，整帧 21 字节。
 *    TYPE=0x90：下位机 -> 上位机，四轮实际速度，整帧 22 字节。
 *    TYPE=0x91：下位机 -> 上位机，IMU 数据，整帧 46 字节。
 *
 * 3. 所有 float 都是 IEEE 754 binary32，小端，连续排列，无结构体填充。
 *    例如 1.0f -> 00 00 80 3F，-2.0f -> 00 00 00 C0。
 *    禁止将带有编译器对齐填充的 C/C++ struct 直接当作线上报文发送。
 *    本文件中的结构体只用于上位机内存，不能据其 sizeof() 推导报文长度。
 *
 * 4. CRC：CRC-16/CCITT-FALSE，poly=0x1021，init=0xFFFF，
 *    refin=false，refout=false，xorout=0x0000。
 *    只覆盖 TYPE 和 PAYLOAD；不覆盖 AA 55，也不覆盖 CRC 自身。
 *    CRC 数值按低字节、高字节顺序发送。例如 0xF6CA -> CA F6。
 *    对 ASCII "123456789" 计算 CRC，应得到 0x29B1。
 *
 * 5. 速度命令 TYPE=0x03，整帧 21 字节，偏移从 0 开始：
 *      [0..1]   AA 55
 *      [2]      03
 *      [3..6]   FL 目标角速度 float32
 *      [7..10]  FR 目标角速度 float32
 *      [11..14] RL 目标角速度 float32
 *      [15..18] RR 目标角速度 float32
 *      [19..20] CRC，覆盖 [2..18] 共 17 字节
 *    四轮同帧更新。0 表示停车，负数表示反转，不逐帧回复 ACK。
 *    MCU 必须拒绝 NaN/Inf/超范围/CRC错误的命令，且不刷新运动超时。
 *
 * 6. 轮速反馈 TYPE=0x90，整帧 22 字节：
 *      [0..1]   AA 55
 *      [2]      90
 *      [3..6]   FL 实际角速度 float32
 *      [7..10]  FR 实际角速度 float32
 *      [11..14] RL 实际角速度 float32
 *      [15..18] RR 实际角速度 float32
 *      [19]     valid_flags，bit0=FL，bit1=FR，bit2=RL，bit3=RR
 *      [20..21] CRC，覆盖 [2..19] 共 18 字节
 *    有效位 1=可用，0=不可用；高 4 位发送为 0。四轮全有效为 0x0F。
 *    上传编码器测得的真实速度，不能用目标速度冒充。停车时也继续上传。
 *
 * 7. IMU 反馈 TYPE=0x91，整帧 46 字节：
 *      [0..1]   AA 55
 *      [2]      91
 *      [3..6]   qx float32        [7..10]  qy float32
 *      [11..14] qz float32        [15..18] qw float32
 *      [19..22] gx float32        [23..26] gy float32
 *      [27..30] gz float32        （角速度，rad/s）
 *      [31..34] ax float32        [35..38] ay float32
 *      [39..42] az float32        （加速度计比力，m/s^2，不预先去重力）
 *      [43]     valid_flags，bit0=四元数，bit1=角速度，bit2=加速度
 *      [44..45] CRC，覆盖 [2..43] 共 42 字节
 *    高 5 位发送为 0；三组全有效为 0x07，姿态未就绪但其余有效为 0x06。
 *    全部数据使用同一右手 IMU 传感器坐标系；轴向由硬件安装说明给出。
 *    q=(qx,qy,qz,qw) 表示传感器系相对 z 向上世界系的姿态，须归一化。
 *    本解析器接受 |四元数模长-1| <= 0.01，并在上位机再次归一化。
 *    无磁力计时可用启动时相对 yaw，不得冒充绝对北向。
 *    IMU 的 z 轴向上且静止时，az 约为 +9.81。不要额外发送欧拉角。
 *
 * 8. 频率与有效性：三种帧默认各 50 Hz，轮速和 IMU 使用独立反馈帧。
 *    初始化/采样失败时清除相关有效位。无效字段可填 0，但不能置有效。
 *    MCU 只有在产生新采样时才把数据标为有效，不能反复重发旧缓存。
 *    正式通信 UART 不混入 printf 文本；不把 UART 一次读取当作一帧。
 *
 * 9. MCU 本地停车要求（本解析器不能替 MCU 执行）：
 *    上电/复位/速度命令超时后保持零目标；先收到合法零速度帧，再允许运动。
 *    连续 200 ms 未收到合法速度帧，清零目标并执行停机策略。
 *    失效帧不能刷新此计时。命令改为 0 不等于物理上瞬间停稳。
 *
 * ======================== 双方核对用完整十六进制帧 =======================
 *
 * 四轮目标 [0,0,0,0]，21 字节，CRC 数值=0xF6CA：
 * AA 55 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 CA F6
 *
 * 四轮目标 [1,-2,3,-4] rad/s，21 字节，CRC 数值=0x02FD：
 * AA 55 03 00 00 80 3F 00 00 00 C0 00 00 40 40 00 00 80 C0 FD 02
 *
 * 四轮实际 [1,-2,3,-4] rad/s，全部有效，22 字节，CRC 数值=0x393F：
 * AA 55 90 00 00 80 3F 00 00 00 C0 00 00 40 40 00 00 80 C0 0F 3F 39
 *
 * IMU q=[0,0,0,1]，g=[0,0,0]，a=[0,0,9.81]，全部有效，46 字节：
 * AA 55 91 00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 3F
 * 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * C3 F5 1C 41 07 24 9A
 * 上述三行拼成一帧；9.81f 是 binary32 近似值，CRC 数值=0x9A24。
 *
 * ======================== ROS 2 硬件插件接入边界 =========================
 *
 * 本模块是打包/解析器，可以编入 hardware_interface::SystemInterface
 * 插件的 .so；它本身不是可由 controller_manager 直接加载的硬件插件。
 * 麦轮控制器轮关节接口依据：
 * https://control.ros.org/humble/doc/ros2_controllers/mecanum_drive_controller/doc/userdoc.html
 * 串口打开、非阻塞读写、write() 部分写入处理、生命周期、pluginlib 导出、
 * URDF 关节映射及 CMakeLists.txt 由后续硬件插件实现。
 *
 * 建议在插件 read() 中：
 *   1) 串口读取返回 n>0 时调用 feed(bytes, n)，错误/负数不能转成 size_t。
 *   2) 检查 wheel_feedback_usable()/imu_feedback_usable()，再复制反馈。
 *      轮速不可用时报告故障并安排零速，不能只跳过更新而继续使用旧状态。
 *   3) 轮关节 velocity 状态接口用 wheel_feedback().velocity，单位 rad/s。
 *   4) IMU 接 orientation、angular_velocity、linear_acceleration 接口。
 * 建议在插件 write() 中：
 *   调用 encode_speed_command()，仅成功时发送整帧，妥善处理部分写入。
 *   若返回 false，应报告错误并安排零速度停机，不能发送旧非零命令。
 * 断线重连调用 reset()，丢弃旧数据及残帧，并先发送合法零速度命令。
 *
 * 接收超时采用上位机内部 steady_clock，不在线上传时间字段。超过默认
 * 200 ms 后 usable() 返回 false，但 getter 仍保留快照供诊断，不能直接用。
 * 未带序号/采样时间的协议无法精确统计丢帧或识别连续重放的旧数据。
 *
 * 线程约定：实例没有内部锁。feed/reset/getter/usable/statistics 应在
 * 同一线程使用，或由调用者同步；不能让串口线程与 ROS 线程无锁读写。
 * feed() 内使用固定数组，不进行动态内存分配，只保存每类最新一帧。
 */

namespace x5car
{

using WheelVelocities = std::array<double, 4>;  // FL, FR, RL, RR，rad/s。

struct WheelFeedback
{
  WheelVelocities velocity{};
  std::uint8_t valid_flags{0};
};

struct ImuFeedback
{
  std::array<double, 4> orientation{};  // qx, qy, qz, qw。
  std::array<double, 3> angular_velocity{};  // gx, gy, gz，rad/s。
  std::array<double, 3> linear_acceleration{};  // ax, ay, az，m/s^2。
  std::uint8_t valid_flags{0};
};

// 仅为上位机本地诊断，不在线上传；没有帧序号，不能据此精确推断丢帧数。
struct ProtocolStatistics
{
  std::uint64_t wheel_frames{0};
  std::uint64_t imu_frames{0};
  std::uint64_t crc_errors{0};
  std::uint64_t unknown_types{0};
  std::uint64_t invalid_numeric_groups{0};
  std::uint64_t ignored_command_frames{0};
  std::uint64_t discarded_bytes{0};
};

class SerialProtocol final
{
public:
  static constexpr std::uint8_t kHeaderFirst = 0xAA;
  static constexpr std::uint8_t kHeaderSecond = 0x55;
  static constexpr std::uint8_t kSpeedCommandType = 0x03;
  static constexpr std::uint8_t kWheelFeedbackType = 0x90;
  static constexpr std::uint8_t kImuFeedbackType = 0x91;

  static constexpr std::size_t kSpeedCommandSize = 21;
  static constexpr std::size_t kWheelFeedbackSize = 22;
  static constexpr std::size_t kImuFeedbackSize = 46;

  static constexpr std::uint8_t kAllWheelsValid = 0x0F;
  static constexpr std::uint8_t kOrientationValid = 0x01;
  static constexpr std::uint8_t kAngularVelocityValid = 0x02;
  static constexpr std::uint8_t kLinearAccelerationValid = 0x04;
  static constexpr std::uint8_t kAllImuValid = 0x07;
  static constexpr double kQuaternionNormTolerance = 0.01;

  using SpeedCommandFrame = std::array<std::uint8_t, kSpeedCommandSize>;
  using Clock = std::chrono::steady_clock;

  SerialProtocol() noexcept;

  // 清空残帧、反馈和诊断计数；所有数据初始无效，数值为 quiet_NaN。
  // 插件重连/重新激活时调用，防止跨会话使用旧测量。
  void reset() noexcept;

  // 任意分块输入接收到的字节；支持一次半帧、多帧、噪声和 CRC 错误。
  // 返回本次解出的轮速/IMU 帧总数（包含有效位全为 0 的状态通知帧）。
  // CRC 正确不代表传感器有效，必须再检查 usable() 或对应有效位。
  // nullptr 或 size==0 返回 0；仅存储每类最新帧，不保留历史队列。
  std::size_t feed(const std::uint8_t * data, std::size_t size) noexcept;

  const WheelFeedback & wheel_feedback() const noexcept;
  const ImuFeedback & imu_feedback() const noexcept;
  const ProtocolStatistics & statistics() const noexcept;

  // 默认要求最近 200 ms 内的最新轮速帧中四轮全部有效。
  // 无效新帧会立即替换旧帧，使本函数返回 false，而非等旧帧超时。
  bool wheel_feedback_usable(
    std::chrono::milliseconds timeout = std::chrono::milliseconds{200}) const noexcept;

  // required_flags 可选择本次需要的 IMU 数据组，默认要求三组均有效。
  // 例如只用角速度：imu_feedback_usable(kAngularVelocityValid)。
  // required_flags 为 0 或含未知位、timeout<=0 时返回 false。
  bool imu_feedback_usable(
    std::uint8_t required_flags = kAllImuValid,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{200}) const noexcept;

  // 将 ROS 使用的 double 速度转换为小端 float32，并生成 21 字节命令。
  // max_abs_speed_rad_s 为四轮共同采用的允许上限，必须按实车参数提供。
  // 上限须为正且能用 float32 表示；所有目标及转换后的值均须在范围内。
  // 不静默限幅；任何参数不合法返回 false，并将 out 全部清零（禁止发送）。
  // 成功返回 true；停车也调用此函数，目标传 {0,0,0,0}。
  static bool encode_speed_command(
    const WheelVelocities & target_rad_s,
    double max_abs_speed_rad_s,
    SpeedCommandFrame & out) noexcept;

  // 公开给双方核对 CRC。对 TYPE+PAYLOAD 调用，不包含帧头和 CRC。
  // size==0 返回初值 0xFFFF；nullptr 且 size>0 视为调用错误，返回 0。
  static std::uint16_t crc16_ccitt_false(
    const std::uint8_t * data, std::size_t size) noexcept;

private:
  static std::size_t frame_size(std::uint8_t type) noexcept;
  static float read_float32_le(const std::uint8_t * bytes) noexcept;
  static void write_float32_le(float value, std::uint8_t * bytes) noexcept;

  void discard_front(std::size_t count) noexcept;
  std::size_t parse_available() noexcept;
  void decode_wheel_frame() noexcept;
  void decode_imu_frame() noexcept;

  std::array<std::uint8_t, kImuFeedbackSize> rx_buffer_{};
  std::size_t rx_size_{0};
  WheelFeedback wheel_{};
  ImuFeedback imu_{};
  ProtocolStatistics statistics_{};
  Clock::time_point wheel_received_at_{};
  Clock::time_point imu_received_at_{};
  bool have_wheel_frame_{false};
  bool have_imu_frame_{false};
};

}  // namespace x5car

#endif  // X5CAR_PROTOCOL_HPP_
