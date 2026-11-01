#ifndef X5CAR_HARDWARE_SERIAL_PORT_HPP_
#define X5CAR_HARDWARE_SERIAL_PORT_HPP_
#include "x5car_hardware/x5car_protocol.hpp"
#include <chrono>
#include <cstddef>
#include <string>

namespace x5car_hardware {
// Linux UART 专用。实例只在控制线程/已串行化的生命周期回调内使用。
// 最多保留一个正在发送的帧和一个最新待发帧；绝不无限积压运动指令。
class SerialPort final {
public:
  using Frame = x5car::SerialProtocol::SpeedCommandFrame;
  using WheelObserver = void (*)(const x5car::WheelFeedback &, void *);
  SerialPort() = default;
  ~SerialPort();
  SerialPort(const SerialPort &) = delete;
  SerialPort & operator=(const SerialPort &) = delete;
  bool open(const std::string & path);
  void close() noexcept;
  bool is_open() const noexcept { return fd_ >= 0; }
  int last_error() const noexcept { return error_; }
  bool clear_buffers() noexcept;  // 只在连接/激活边界调用
  // observer 可选，只在停车确认时逐帧观察；常规 read 仍批量解码。
  bool receive(x5car::SerialProtocol & protocol,
    WheelObserver observer = nullptr, void * context = nullptr) noexcept;
  bool submit(const Frame & frame) noexcept;
  bool pump() noexcept;
  void discard_unsent() noexcept; // 已经发出部分字节的帧必须先完成
  bool idle() const noexcept { return !pending_ && !next_pending_; }
private:
  using Clock = std::chrono::steady_clock;
  int fd_{-1};
  int error_{0};
  Frame current_{}, next_{};
  std::size_t offset_{0};
  bool pending_{false}, next_pending_{false};
  Clock::time_point started_{};
};
}
#endif
