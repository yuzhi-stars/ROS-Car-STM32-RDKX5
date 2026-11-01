#include "x5car_hardware/serial_port.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace x5car_hardware {
SerialPort::~SerialPort() { close(); }
bool SerialPort::open(const std::string & path) {
  close();
  error_ = 0;
  fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd_ < 0) { error_ = errno; return false; }
  auto fail = [this]() { const int saved = errno; close(); error_ = saved; return false; };
  // 防止其他进程再次打开同一个 UART。不要同时启动独立 IMU 串口节点。
  if (::ioctl(fd_, TIOCEXCL) < 0) { return fail(); }
  termios config{};
  if (::tcgetattr(fd_, &config) < 0) { return fail(); }
  ::cfmakeraw(&config);
  if (::cfsetispeed(&config, B115200) < 0 || ::cfsetospeed(&config, B115200) < 0) {
    return fail();
  }
  config.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
  config.c_cflag |= CS8 | CLOCAL | CREAD;
  config.c_iflag &= ~(IXON | IXOFF | IXANY);
  config.c_cc[VMIN] = 0;
  config.c_cc[VTIME] = 0;
  if (::tcsetattr(fd_, TCSANOW, &config) < 0 || !clear_buffers()) { return fail(); }
  return true;
}
void SerialPort::close() noexcept {
  if (fd_ >= 0) { ::close(fd_); }
  fd_ = -1;
  pending_ = next_pending_ = false;
  offset_ = 0;
}
bool SerialPort::clear_buffers() noexcept {
  if (fd_ < 0) { error_ = EBADF; return false; }
  if (::tcflush(fd_, TCIOFLUSH) < 0) { error_ = errno; return false; }
  pending_ = next_pending_ = false;
  offset_ = 0;
  return true;
}
bool SerialPort::receive(x5car::SerialProtocol & protocol,
  WheelObserver observer, void * context) noexcept {
  if (fd_ < 0) { error_ = EBADF; return false; }
  std::array<std::uint8_t, 256> buffer{};
  // 每个周期最多读 1024 字节，噪声流不能无限占用 controller_manager。
  for (int attempt = 0; attempt < 4; ++attempt) {
    const ssize_t count = ::read(fd_, buffer.data(), buffer.size());
    if (count > 0) {
      if (observer == nullptr) {
        protocol.feed(buffer.data(), static_cast<std::size_t>(count));
      } else {
        // feed() 只保存最新帧。停车时按字节送入并逐个观察新轮速帧，
        // 防止同一批 [零速、非零、零速] 的中间异常被最后一帧掩盖。
        for (ssize_t i = 0; i < count; ++i) {
          const auto previous = protocol.statistics().wheel_frames;
          protocol.feed(buffer.data() + i, 1);
          if (protocol.statistics().wheel_frames != previous)
            observer(protocol.wheel_feedback(), context);
        }
      }
      continue;
    }
    if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) { return true; }
    if (errno == EINTR) { continue; }
    error_ = errno;
    return false;
  }
  return true;
}
bool SerialPort::submit(const Frame & frame) noexcept {
  if (fd_ < 0) { error_ = EBADF; return false; }
  if (!pending_) {
    current_ = frame;
    offset_ = 0;
    pending_ = true;
    started_ = Clock::now();
  } else if (offset_ == 0) {
    current_ = frame;  // 尚未发出，可用最新目标替换；不刷新原排队截止时间
  } else {
    next_ = frame;
    next_pending_ = true;
  }
  return pump();
}
bool SerialPort::pump() noexcept {
  if (fd_ < 0) { error_ = EBADF; return false; }
  for (int attempt = 0; attempt < 4 && pending_; ++attempt) {
    if (Clock::now() - started_ >= std::chrono::milliseconds{50}) {
      error_ = ETIMEDOUT;
      return false;
    }
    const ssize_t count = ::write(fd_, current_.data() + offset_, current_.size() - offset_);
    if (count > 0) {
      offset_ += static_cast<std::size_t>(count);
      if (offset_ == current_.size()) {
        pending_ = false;
        offset_ = 0;
        if (next_pending_) {
          current_ = next_;
          next_pending_ = false;
          pending_ = true;
          started_ = Clock::now();
        }
      }
      continue;
    }
    if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) { return true; }
    if (errno == EINTR) { continue; }
    error_ = errno;
    return false;
  }
  return true;
}
void SerialPort::discard_unsent() noexcept {
  next_pending_ = false;
  if (offset_ == 0) { pending_ = false; }
}
}
