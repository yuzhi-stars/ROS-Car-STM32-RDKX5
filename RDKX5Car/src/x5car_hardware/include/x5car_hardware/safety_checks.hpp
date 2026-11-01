#ifndef X5CAR_HARDWARE_SAFETY_CHECKS_HPP_
#define X5CAR_HARDWARE_SAFETY_CHECKS_HPP_
#include "x5car_hardware/x5car_protocol.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace x5car_hardware {
// 只使用 steady_clock，不受 ROS /clock 或系统校时影响。
// read 和 write 分别更新时间；反复调用其中一方不能掩盖另一方停止。
class ControlLoopWatchdog final {
public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::milliseconds kTimeout{100};
  void arm(Clock::time_point now = Clock::now()) noexcept {
    last_read_ = last_write_ = now;
    armed_ = true;
  }
  void disarm() noexcept { armed_ = false; }
  bool read_tick(Clock::time_point now = Clock::now()) noexcept {
    if (!healthy(now)) return false;
    last_read_ = now;
    return true;
  }
  bool write_tick(Clock::time_point now = Clock::now()) noexcept {
    if (!healthy(now)) return false;
    last_write_ = now;
    return true;
  }
private:
  bool healthy(Clock::time_point now) const noexcept {
    return armed_ && now - last_read_ < kTimeout && now - last_write_ < kTimeout;
  }
  Clock::time_point last_read_{}, last_write_{};
  bool armed_{false};
};

// 必须逐个新解出的轮速帧调用 observe；不能只观察一次 read 的最后一帧。
// 只有跨越至少 200 ms 的连续有效零速样本才能确认；单批缓存零帧不够。
class StopConfirmation final {
public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::milliseconds kStableDuration{200};
  StopConfirmation(double threshold, std::chrono::milliseconds max_gap) noexcept
  : threshold_(threshold), max_gap_(max_gap) {}
  void observe(const x5car::WheelFeedback & wheel,
    Clock::time_point now = Clock::now()) noexcept {
    if (have_sample_ && now - last_sample_ >= max_gap_) stable_ = false;
    last_sample_ = now;
    have_sample_ = true;
    const bool zero = wheel.valid_flags == x5car::SerialProtocol::kAllWheelsValid &&
      std::all_of(wheel.velocity.begin(), wheel.velocity.end(), [this](double value) {
        return std::isfinite(value) && std::abs(value) <= threshold_;
      });
    if (!zero) { stable_ = false; return; }
    if (!stable_) { since_ = now; stable_ = true; }
  }
  bool confirmed(Clock::time_point now = Clock::now()) const noexcept {
    // 稳定区间必须由实际新样本覆盖，不能只在一帧之后等待 200 ms。
    return have_sample_ && stable_ && last_sample_ - since_ >= kStableDuration &&
      now - last_sample_ < max_gap_;
  }
private:
  double threshold_;
  std::chrono::milliseconds max_gap_;
  Clock::time_point since_{}, last_sample_{};
  bool have_sample_{false}, stable_{false};
};
}
#endif
