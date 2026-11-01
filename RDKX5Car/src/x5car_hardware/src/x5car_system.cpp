#include "x5car_hardware/x5car_system.hpp"
#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "pluginlib/class_list_macros.hpp"
#include "rclcpp/rclcpp.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>

namespace x5car_hardware {
using CallbackReturn = hardware_interface::CallbackReturn;
using Return = hardware_interface::return_type;
namespace {
auto logger() { return rclcpp::get_logger("x5car_hardware"); }
const std::array<std::string, 10> imu_interfaces = {
  "orientation.x", "orientation.y", "orientation.z", "orientation.w",
  "angular_velocity.x", "angular_velocity.y", "angular_velocity.z",
  "linear_acceleration.x", "linear_acceleration.y", "linear_acceleration.z"};
}
X5CarSystem::~X5CarSystem() { serial_.close(); } // 进程强杀不保证析构，MCU 必须独立超时停车
CallbackReturn X5CarSystem::on_init(const hardware_interface::HardwareInfo & info) {
  if (SystemInterface::on_init(info) != CallbackReturn::SUCCESS) { return CallbackReturn::ERROR; }
  try {
    auto text = [this](const std::string & key) { return info_.hardware_parameters.at(key); };
    auto number = [&text](const std::string & key) {
      const auto value = text(key);
      std::size_t used = 0;
      const double parsed = std::stod(value, &used);
      if (used != value.size() || !std::isfinite(parsed)) { throw std::runtime_error(key); }
      return parsed;
    };
    port_ = text("serial_port");
    imu_name_ = text("imu_sensor_name");
    max_speed_ = number("max_wheel_speed_rad_s");
    stopped_threshold_ = number("stopped_threshold_rad_s");
    if (port_.empty() || imu_name_.empty() || max_speed_ <= 0 || stopped_threshold_ < 0) {
      throw std::runtime_error("configure a real serial port and positive motor speed limit");
    }
    auto timeout = [&number](const std::string & key) {
      const double value = number(key);
      if (value < 1 || value > 60000 || std::floor(value) != value) { throw std::runtime_error(key); }
      return std::chrono::milliseconds{static_cast<long long>(value)};
    };
    feedback_timeout_ = timeout("feedback_timeout_ms");
    activate_timeout_ = timeout("activate_timeout_ms");
    stop_timeout_ = timeout("stop_timeout_ms");
    if (activate_timeout_ <= StopConfirmation::kStableDuration ||
      stop_timeout_ <= StopConfirmation::kStableDuration) {
      throw std::runtime_error("activate/stop timeout must exceed the 200ms stable-stop interval");
    }
    auto require = text("require_imu");
    std::transform(require.begin(), require.end(), require.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (require != "true" && require != "false") { throw std::runtime_error("require_imu must be true/false"); }
    require_imu_ = require == "true";
    const std::array<std::string, 4> keys = {"fl_joint", "fr_joint", "rl_joint", "rr_joint"};
    for (std::size_t i = 0; i < keys.size(); ++i) { joint_names_[i] = text(keys[i]); }
    if (std::set<std::string>(joint_names_.begin(), joint_names_.end()).size() != 4 || info_.joints.size() != 4) {
      throw std::runtime_error("exactly four unique wheel joints required");
    }
    for (const auto & name : joint_names_) {
      const auto found = std::find_if(info_.joints.begin(), info_.joints.end(),
        [&name](const auto & j) { return j.name == name; });
      if (found == info_.joints.end() || found->command_interfaces.size() != 1 ||
        found->command_interfaces[0].name != "velocity") { throw std::runtime_error("wheel command interface"); }
      std::set<std::string> states;
      for (const auto & state : found->state_interfaces) { states.insert(state.name); }
      if (states != std::set<std::string>{"position", "velocity"}) { throw std::runtime_error("wheel state interfaces"); }
    }
    if (info_.sensors.size() != 1 || info_.sensors[0].name != imu_name_) { throw std::runtime_error("IMU sensor name"); }
    std::set<std::string> actual;
    for (const auto & state : info_.sensors[0].state_interfaces) { actual.insert(state.name); }
    if (actual != std::set<std::string>(imu_interfaces.begin(), imu_interfaces.end())) {
      throw std::runtime_error("IMU needs orientation/gyro/acceleration interfaces");
    }
    x5car::SerialProtocol::SpeedCommandFrame zero{};
    if (!x5car::SerialProtocol::encode_speed_command({0, 0, 0, 0}, max_speed_, zero)) {
      throw std::runtime_error("motor limit is not representable as float32");
    }
  } catch (const std::exception & e) {
    RCLCPP_ERROR(logger(), "Invalid hardware configuration: %s", e.what());
    return CallbackReturn::ERROR;
  }
  command_.fill(0.0);
  position_.fill(0.0);
  invalidate();
  return CallbackReturn::SUCCESS;
}
std::vector<hardware_interface::StateInterface> X5CarSystem::export_state_interfaces() {
  std::vector<hardware_interface::StateInterface> interfaces;
  for (std::size_t i = 0; i < 4; ++i) {
    interfaces.emplace_back(joint_names_[i], "position", &position_[i]);
    interfaces.emplace_back(joint_names_[i], "velocity", &velocity_[i]);
  }
  for (std::size_t i = 0; i < imu_interfaces.size(); ++i) {
    double * value = i < 4 ? &orientation_[i] : (i < 7 ? &angular_velocity_[i - 4] : &acceleration_[i - 7]);
    interfaces.emplace_back(imu_name_, imu_interfaces[i], value);
  }
  return interfaces;
}
std::vector<hardware_interface::CommandInterface> X5CarSystem::export_command_interfaces() {
  std::vector<hardware_interface::CommandInterface> interfaces;
  for (std::size_t i = 0; i < 4; ++i) { interfaces.emplace_back(joint_names_[i], "velocity", &command_[i]); }
  return interfaces;
}
void X5CarSystem::invalidate() {
  const double invalid = std::numeric_limits<double>::quiet_NaN();
  velocity_.fill(invalid);
  orientation_.fill(invalid);
  angular_velocity_.fill(invalid);
  acceleration_.fill(invalid);
}
bool X5CarSystem::feedback_ok() const {
  // 当前联调暂不要求姿态四元数；仍要求陀螺仪与加速度有效且新鲜。
  // 恢复完整姿态约束时将 required_imu 改回 kAllImuValid，不改线协议。
  constexpr auto required_imu = static_cast<std::uint8_t>(
    x5car::SerialProtocol::kAngularVelocityValid | x5car::SerialProtocol::kLinearAccelerationValid);
  return protocol_.wheel_feedback_usable(feedback_timeout_) &&
    (!require_imu_ || protocol_.imu_feedback_usable(required_imu, feedback_timeout_));
}
void X5CarSystem::copy_feedback() {
  velocity_ = protocol_.wheel_feedback().velocity;
  const auto & imu = protocol_.imu_feedback();
  orientation_ = imu.orientation;
  angular_velocity_ = imu.angular_velocity;
  acceleration_ = imu.linear_acceleration;
}
CallbackReturn X5CarSystem::on_configure(const rclcpp_lifecycle::State &) {
  active_ = false;
  faulted_ = false;
  loop_watchdog_.disarm();
  command_.fill(0.0);
  position_.fill(0.0);
  protocol_.reset();
  invalidate();
  if (!serial_.open(port_)) {
    RCLCPP_ERROR(logger(), "Cannot open %s (errno=%d)", port_.c_str(), serial_.last_error());
    return CallbackReturn::FAILURE;
  }
  return CallbackReturn::SUCCESS;
}
// 生命周期有界等待：50 Hz 发零速，接收反馈；只在生命周期中 sleep。
bool X5CarSystem::zero_until(std::chrono::milliseconds duration, bool confirm_stopped) {
  if (!serial_.is_open()) { return false; }
  x5car::SerialProtocol::SpeedCommandFrame zero{};
  if (!x5car::SerialProtocol::encode_speed_command({0, 0, 0, 0}, max_speed_, zero)) { return false; }
  serial_.discard_unsent();
  using Clock = std::chrono::steady_clock;
  const auto end = Clock::now() + duration;
  auto next_send = Clock::now();
  StopConfirmation stop_check(stopped_threshold_, feedback_timeout_);
  const SerialPort::WheelObserver observe = [](const x5car::WheelFeedback & wheel, void * context) {
    static_cast<StopConfirmation *>(context)->observe(wheel);
  };
  bool sent = false;
  while (Clock::now() < end) {
    if (Clock::now() >= next_send) {
      if (!serial_.submit(zero)) { return false; }
      next_send = Clock::now() + std::chrono::milliseconds{20};
      sent = true;
    }
    if (!serial_.pump() || !serial_.receive(protocol_,
      confirm_stopped ? observe : nullptr, &stop_check)) { return false; }
    if (sent && serial_.idle()) {
      if (!confirm_stopped) { return true; } // 仅确认本地发送完成，不冒充已停稳
      if (stop_check.confirmed() && protocol_.wheel_feedback_usable(feedback_timeout_))
        return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return false;
}
CallbackReturn X5CarSystem::on_activate(const rclcpp_lifecycle::State &) {
  active_ = false;
  loop_watchdog_.disarm();
  command_.fill(0.0);
  invalidate();
  protocol_.reset();
  const auto deadline = std::chrono::steady_clock::now() + activate_timeout_;
  if (!serial_.clear_buffers() || !zero_until(activate_timeout_, true)) { return CallbackReturn::FAILURE; }
  // 已连续确认零速；IMU 的陀螺仪/加速度可能仍在校准，继续发零等待。
  while (!feedback_ok() && std::chrono::steady_clock::now() < deadline) {
    if (!zero_until(std::chrono::milliseconds{25}, false)) { return CallbackReturn::FAILURE; }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  if (!feedback_ok()) { zero_until(std::chrono::milliseconds{20}, false); return CallbackReturn::FAILURE; }
  copy_feedback();
  faulted_ = false; // 只有显式激活并完成零速握手后才解除本地故障锁定。
  loop_watchdog_.arm();
  active_ = true;
  return CallbackReturn::SUCCESS;
}
CallbackReturn X5CarSystem::on_deactivate(const rclcpp_lifecycle::State &) {
  active_ = false;
  loop_watchdog_.disarm();
  command_.fill(0.0);
  return zero_until(stop_timeout_, true) ? CallbackReturn::SUCCESS : CallbackReturn::ERROR;
}
bool X5CarSystem::cleanup_link(bool confirm_stop) {
  active_ = false;
  loop_watchdog_.disarm();
  command_.fill(0.0);
  const bool stopped = !serial_.is_open() || zero_until(
    confirm_stop ? stop_timeout_ : std::chrono::milliseconds{20}, confirm_stop);
  serial_.close();
  protocol_.reset();
  invalidate();
  return stopped;
}
CallbackReturn X5CarSystem::on_cleanup(const rclcpp_lifecycle::State &) {
  cleanup_link(false);
  return CallbackReturn::SUCCESS;
}
CallbackReturn X5CarSystem::on_shutdown(const rclcpp_lifecycle::State &) {
  return cleanup_link(true) ? CallbackReturn::SUCCESS : CallbackReturn::ERROR;
}
CallbackReturn X5CarSystem::on_error(const rclcpp_lifecycle::State &) {
  if (!cleanup_link(false)) { RCLCPP_ERROR(logger(), "Stop frame unconfirmed; MCU timeout must stop motors"); }
  return CallbackReturn::SUCCESS; // 已清理，可重新 configure；不表示确认了物理停车
}
Return X5CarSystem::fail(const char * reason, bool discard_buffers) {
  active_ = false;
  faulted_ = true;
  loop_watchdog_.disarm();
  command_.fill(0.0);
  if (discard_buffers) {
    // 必须先丢掉操作系统积压数据与解析残帧，再安排零速；
    // 不能重新解析旧数据，把一次控制中断立即变回“反馈正常”。
    if (serial_.is_open()) serial_.clear_buffers();
    protocol_.reset();
  }
  serial_.discard_unsent();
  x5car::SerialProtocol::SpeedCommandFrame zero{};
  if (serial_.is_open() && x5car::SerialProtocol::encode_speed_command(command_, max_speed_, zero)) {
    serial_.submit(zero); // 非阻塞尝试；on_error 再清理，不能依赖下一次 write()
  }
  invalidate();
  RCLCPP_ERROR(logger(), "Hardware fault: %s (errno=%d)", reason, serial_.last_error());
  return Return::ERROR;
}
Return X5CarSystem::read(const rclcpp::Time &, const rclcpp::Duration & period) {
  if (faulted_) return Return::ERROR;
  if (active_ && !loop_watchdog_.read_tick())
    return fail("control loop interrupted before read (>=100ms)", true);
  if (!serial_.receive(protocol_)) { return fail("serial read"); }
  if (!feedback_ok()) {
    invalidate();
    return active_ ? fail("invalid or stale wheel/IMU feedback") : Return::OK;
  }
  copy_feedback();
  // 没有累计编码器计数：积分得到的关节角只用于模型显示，不能冒充绝对编码器。
  const double dt = period.seconds();
  if (dt > 0 && dt < 0.2) {
    for (std::size_t i = 0; i < 4; ++i) { position_[i] += velocity_[i] * dt; }
  }
  return Return::OK;
}
Return X5CarSystem::write(const rclcpp::Time &, const rclcpp::Duration &) {
  if (faulted_) return Return::ERROR;
  if (!active_) { return Return::OK; }
  if (!loop_watchdog_.write_tick())
    return fail("control loop interrupted before write (>=100ms)", true);
  if (!feedback_ok()) { return fail("feedback expired before write"); }
  auto target = command_;
  // 命令格式问题 != 硬件通信故障。不要因为单次非法轮速让整个
  // ros2_control 硬件组件掉到 UNCONFIGURED；但绝不能把非法命令发给 STM32。
  const bool all_nan = std::all_of(target.begin(), target.end(),
    [](double value) { return std::isnan(value); });
  if (all_nan) {
    // 控制器停用/尚未输出有效命令：安全地解释为零速。
    target.fill(0.0);
  } else {
    const bool non_finite = std::any_of(target.begin(), target.end(),
      [](double value) { return !std::isfinite(value); });
    const bool over_limit = std::any_of(target.begin(), target.end(),
      [this](double value) { return std::isfinite(value) && std::abs(value) > max_speed_; });
    if (non_finite || over_limit) {
      // 对部分 NaN / Inf / 超速：整组拒绝，清除尚未发送的旧命令，并发送零速。
      // 不用截断到 max_speed_：任何异常都不能产生非零运动命令。
      static rclcpp::Clock diagnostic_clock(RCL_STEADY_TIME);
      RCLCPP_WARN_THROTTLE(logger(), diagnostic_clock, 1000,
        "Rejected wheel speeds (%s): FL=%g FR=%g RL=%g RR=%g, "
        "limit=%g rad/s; transmitting ZERO (hardware remains active)",
        non_finite ? "NaN/Inf" : "out of range",
        target[0], target[1], target[2], target[3], max_speed_);
      target.fill(0.0);
      serial_.discard_unsent();
    }
  }

  x5car::SerialProtocol::SpeedCommandFrame frame{};
  if (!x5car::SerialProtocol::encode_speed_command(target, max_speed_, frame)) {
    // 已经过有限值/限速检查；如果这里还失败，则可能是协议编码器内部错误。
    // 保留硬件故障处理，以免忽略未知的串口协议错误。
    RCLCPP_ERROR(logger(),
      "Encoder rejected validated speeds: FL=%g FR=%g RL=%g RR=%g, limit=%g rad/s",
      target[0], target[1], target[2], target[3], max_speed_);
    return fail("wheel command encoder failed after validation");
  }
  if (!serial_.submit(frame)) { return fail("serial write/queue timeout"); }
  return Return::OK;
}
}
PLUGINLIB_EXPORT_CLASS(x5car_hardware::X5CarSystem, hardware_interface::SystemInterface)
