#ifndef X5CAR_HARDWARE_SYSTEM_HPP_
#define X5CAR_HARDWARE_SYSTEM_HPP_
#include "x5car_hardware/serial_port.hpp"
#include "x5car_hardware/safety_checks.hpp"
#include "hardware_interface/system_interface.hpp"
#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace x5car_hardware {
class X5CarSystem final : public hardware_interface::SystemInterface {
public:
  ~X5CarSystem() override;
  hardware_interface::CallbackReturn on_init(const hardware_interface::HardwareInfo &) override;
  std::vector<hardware_interface::StateInterface> export_state_interfaces() override;
  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;
  hardware_interface::CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  hardware_interface::CallbackReturn on_error(const rclcpp_lifecycle::State &) override;
  hardware_interface::return_type read(const rclcpp::Time &, const rclcpp::Duration &) override;
  hardware_interface::return_type write(const rclcpp::Time &, const rclcpp::Duration &) override;
private:
  bool feedback_ok() const;
  void copy_feedback();
  void invalidate();
  bool zero_until(std::chrono::milliseconds duration, bool confirm_stopped);
  bool cleanup_link(bool confirm_stop);
  hardware_interface::return_type fail(const char * reason, bool discard_buffers = false);
  SerialPort serial_;
  x5car::SerialProtocol protocol_;
  ControlLoopWatchdog loop_watchdog_;
  x5car::WheelVelocities command_{}, velocity_{}, position_{};
  std::array<double, 4> orientation_{};
  std::array<double, 3> angular_velocity_{}, acceleration_{};
  std::array<std::string, 4> joint_names_{};  // 固定 FL / FR / RL / RR
  std::string port_, imu_name_;
  double max_speed_{0.0}, stopped_threshold_{0.05};
  std::chrono::milliseconds feedback_timeout_{200}, activate_timeout_{3000}, stop_timeout_{500};
  bool active_{false}, faulted_{false}, require_imu_{true};
};
}
#endif
