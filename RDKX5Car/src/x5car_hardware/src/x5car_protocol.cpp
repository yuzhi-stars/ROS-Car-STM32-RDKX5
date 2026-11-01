#include "x5car_hardware/x5car_protocol.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace x5car
{
namespace
{

// 协议要求 binary32。RDK X5/Linux 满足；其他平台不满足时编译即报错。
static_assert(sizeof(float) == 4, "Protocol requires a 4-byte float");
static_assert(std::numeric_limits<float>::is_iec559, "Protocol requires IEEE 754 float");
static_assert(std::numeric_limits<float>::digits == 24, "Protocol requires binary32");

constexpr double kInvalid = std::numeric_limits<double>::quiet_NaN();

// 原子地判定一组数据是否可用；若不符合，将整个组设为 NaN。
// 一组如三轴角速度中只有一轴异常，也不能把这一组标为完整有效。
template<std::size_t N>
bool finite_group_or_invalidate(std::array<double, N> & values) noexcept
{
  for (const double value : values) {
    if (!std::isfinite(value)) {
      values.fill(kInvalid);
      return false;
    }
  }
  return true;
}

}  // namespace

SerialProtocol::SerialProtocol() noexcept
{
  reset();
}

void SerialProtocol::reset() noexcept
{
  rx_buffer_.fill(0);
  rx_size_ = 0;
  wheel_ = WheelFeedback{};
  imu_ = ImuFeedback{};
  wheel_.velocity.fill(kInvalid);
  imu_.orientation.fill(kInvalid);
  imu_.angular_velocity.fill(kInvalid);
  imu_.linear_acceleration.fill(kInvalid);
  statistics_ = ProtocolStatistics{};
  wheel_received_at_ = Clock::time_point{};
  imu_received_at_ = Clock::time_point{};
  have_wheel_frame_ = false;
  have_imu_frame_ = false;
}

std::uint16_t SerialProtocol::crc16_ccitt_false(
  const std::uint8_t * data, const std::size_t size) noexcept
{
  if (data == nullptr && size != 0) {
    return 0;
  }

  std::uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < size; ++i) {
    crc = static_cast<std::uint16_t>(
      crc ^ (static_cast<std::uint16_t>(data[i]) << 8U));
    for (unsigned int bit = 0; bit < 8; ++bit) {
      // false 反射：每轮检查最高位，左移，最高位为 1 时异或多项式。
      const bool high_bit_set = (crc & 0x8000U) != 0;
      crc = static_cast<std::uint16_t>(crc << 1U);
      if (high_bit_set) {
        crc = static_cast<std::uint16_t>(crc ^ 0x1021U);
      }
    }
  }
  return crc;
}

float SerialProtocol::read_float32_le(const std::uint8_t * bytes) noexcept
{
  // 逐字节拼出位模式，避免未对齐指针、严格别名违规以及主机端序依赖。
  const std::uint32_t bits =
    static_cast<std::uint32_t>(bytes[0]) |
    (static_cast<std::uint32_t>(bytes[1]) << 8U) |
    (static_cast<std::uint32_t>(bytes[2]) << 16U) |
    (static_cast<std::uint32_t>(bytes[3]) << 24U);
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void SerialProtocol::write_float32_le(float value, std::uint8_t * bytes) noexcept
{
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  bytes[0] = static_cast<std::uint8_t>(bits & 0xFFU);
  bytes[1] = static_cast<std::uint8_t>((bits >> 8U) & 0xFFU);
  bytes[2] = static_cast<std::uint8_t>((bits >> 16U) & 0xFFU);
  bytes[3] = static_cast<std::uint8_t>((bits >> 24U) & 0xFFU);
}

bool SerialProtocol::encode_speed_command(
  const WheelVelocities & target_rad_s,
  const double max_abs_speed_rad_s,
  SpeedCommandFrame & out) noexcept
{
  // 失败时清空输出，避免调用者误发上一条非零命令。必须检查返回值。
  out.fill(0);
  const double float_limit = static_cast<double>(std::numeric_limits<float>::max());
  if (!std::isfinite(max_abs_speed_rad_s) || max_abs_speed_rad_s <= 0.0 ||
    max_abs_speed_rad_s > float_limit)
  {
    return false;
  }
  if (static_cast<float>(max_abs_speed_rad_s) <= 0.0F) {
    return false;  // 正的 double 上限过小，转换到 float32 后下溢为 0。
  }

  std::array<float, 4> wire_velocity{};
  for (std::size_t i = 0; i < target_rad_s.size(); ++i) {
    const double target = target_rad_s[i];
    if (!std::isfinite(target) || std::abs(target) > max_abs_speed_rad_s) {
      return false;
    }
    wire_velocity[i] = static_cast<float>(target);
    // double -> float32 的舍入也可能略微越过边界，不能暗中放宽限速。
    if (!std::isfinite(wire_velocity[i]) ||
      std::abs(static_cast<double>(wire_velocity[i])) > max_abs_speed_rad_s)
    {
      return false;
    }
  }

  out[0] = kHeaderFirst;
  out[1] = kHeaderSecond;
  out[2] = kSpeedCommandType;
  for (std::size_t i = 0; i < wire_velocity.size(); ++i) {
    write_float32_le(wire_velocity[i], out.data() + 3 + i * 4);
  }
  const std::uint16_t crc = crc16_ccitt_false(out.data() + 2, 17);
  out[19] = static_cast<std::uint8_t>(crc & 0xFFU);
  out[20] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
  return true;
}

std::size_t SerialProtocol::frame_size(const std::uint8_t type) noexcept
{
  switch (type) {
    case kSpeedCommandType:
      return kSpeedCommandSize;
    case kWheelFeedbackType:
      return kWheelFeedbackSize;
    case kImuFeedbackType:
      return kImuFeedbackSize;
    default:
      return 0;
  }
}

void SerialProtocol::discard_front(const std::size_t count) noexcept
{
  if (count >= rx_size_) {
    rx_size_ = 0;
    return;
  }
  const std::size_t remaining = rx_size_ - count;
  // 源和目的区域重叠，必须使用 memmove，不能用 memcpy。
  std::memmove(rx_buffer_.data(), rx_buffer_.data() + count, remaining);
  rx_size_ = remaining;
}

std::size_t SerialProtocol::feed(
  const std::uint8_t * data, const std::size_t size) noexcept
{
  if (data == nullptr || size == 0) {
    return 0;
  }

  std::size_t feedback_frames = 0;
  for (std::size_t i = 0; i < size; ++i) {
    // 正常流程每写入一个字节即解析，最多留存不足一帧的残片。
    // 防御性边界检查确保异常输入也不可能写出固定缓冲区。
    if (rx_size_ == rx_buffer_.size()) {
      discard_front(1);
      ++statistics_.discarded_bytes;
    }
    rx_buffer_[rx_size_++] = data[i];
    feedback_frames += parse_available();
  }
  return feedback_frames;
}

std::size_t SerialProtocol::parse_available() noexcept
{
  std::size_t feedback_frames = 0;
  while (rx_size_ != 0) {
    // 遇到 AA AA 55 也逐字节滑动，不会遗漏第二个 AA 开始的帧头。
    if (rx_buffer_[0] != kHeaderFirst) {
      discard_front(1);
      ++statistics_.discarded_bytes;
      continue;
    }
    if (rx_size_ < 2) {
      break;  // 保留末尾单独的 AA，等待下一次 feed。
    }
    if (rx_buffer_[1] != kHeaderSecond) {
      discard_front(1);
      ++statistics_.discarded_bytes;
      continue;
    }
    if (rx_size_ < 3) {
      break;
    }

    const std::uint8_t type = rx_buffer_[2];
    const std::size_t expected_size = frame_size(type);
    if (expected_size == 0) {
      ++statistics_.unknown_types;
      discard_front(1);
      ++statistics_.discarded_bytes;
      continue;
    }
    if (rx_size_ < expected_size) {
      break;  // 已知类型决定固定帧长；载荷出现 AA 55 不会提前分帧。
    }

    const std::uint16_t received_crc = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(rx_buffer_[expected_size - 2]) |
      (static_cast<std::uint16_t>(rx_buffer_[expected_size - 1]) << 8U));
    const std::uint16_t calculated_crc =
      crc16_ccitt_false(rx_buffer_.data() + 2, expected_size - 4);
    if (received_crc != calculated_crc) {
      ++statistics_.crc_errors;
      // 坏帧只滑动一字节，不能直接扔掉 expected_size 字节：
      // 真正下一帧的帧头可能已经落在这段失步数据内部。
      discard_front(1);
      ++statistics_.discarded_bytes;
      continue;
    }

    if (type == kWheelFeedbackType) {
      decode_wheel_frame();
      ++statistics_.wheel_frames;
      ++feedback_frames;
    } else if (type == kImuFeedbackType) {
      decode_imu_frame();
      ++statistics_.imu_frames;
      ++feedback_frames;
    } else {
      // 收到命令帧可能是串口回显。只消费，不能冒充任何反馈或保活。
      ++statistics_.ignored_command_frames;
    }
    discard_front(expected_size);
  }
  return feedback_frames;
}

void SerialProtocol::decode_wheel_frame() noexcept
{
  WheelFeedback next{};
  next.velocity.fill(kInvalid);
  const std::uint8_t wire_flags = static_cast<std::uint8_t>(
    rx_buffer_[19] & kAllWheelsValid);  // 忽略保留位，不把它们当作有效轮。

  for (std::size_t i = 0; i < next.velocity.size(); ++i) {
    const auto bit = static_cast<std::uint8_t>(1U << i);
    if ((wire_flags & bit) == 0) {
      continue;
    }
    const double value = static_cast<double>(
      read_float32_le(rx_buffer_.data() + 3 + i * 4));
    if (std::isfinite(value)) {
      next.velocity[i] = value;
      next.valid_flags = static_cast<std::uint8_t>(next.valid_flags | bit);
    } else {
      ++statistics_.invalid_numeric_groups;
    }
  }

  // 即使新帧全无效，也必须替换旧状态，不能继续使用之前的有效轮速。
  wheel_ = next;
  wheel_received_at_ = Clock::now();
  have_wheel_frame_ = true;
}

void SerialProtocol::decode_imu_frame() noexcept
{
  ImuFeedback next{};
  next.orientation.fill(kInvalid);
  next.angular_velocity.fill(kInvalid);
  next.linear_acceleration.fill(kInvalid);
  const std::uint8_t wire_flags = static_cast<std::uint8_t>(
    rx_buffer_[43] & kAllImuValid);

  if ((wire_flags & kOrientationValid) != 0) {
    for (std::size_t i = 0; i < next.orientation.size(); ++i) {
      next.orientation[i] = static_cast<double>(
        read_float32_le(rx_buffer_.data() + 3 + i * 4));
    }
    bool orientation_ok = finite_group_or_invalidate(next.orientation);
    if (orientation_ok) {
      double squared_norm = 0.0;
      for (const double component : next.orientation) {
        squared_norm += component * component;
      }
      const double norm = std::sqrt(squared_norm);
      orientation_ok = norm > 0.0 && std::isfinite(norm) &&
        std::abs(norm - 1.0) <= kQuaternionNormTolerance;
      if (orientation_ok) {
        for (double & component : next.orientation) {
          component /= norm;
        }
      } else {
        next.orientation.fill(kInvalid);
      }
    }
    if (orientation_ok) {
      next.valid_flags = static_cast<std::uint8_t>(next.valid_flags | kOrientationValid);
    } else {
      ++statistics_.invalid_numeric_groups;
    }
  }

  if ((wire_flags & kAngularVelocityValid) != 0) {
    for (std::size_t i = 0; i < next.angular_velocity.size(); ++i) {
      next.angular_velocity[i] = static_cast<double>(
        read_float32_le(rx_buffer_.data() + 19 + i * 4));
    }
    if (finite_group_or_invalidate(next.angular_velocity)) {
      next.valid_flags = static_cast<std::uint8_t>(next.valid_flags | kAngularVelocityValid);
    } else {
      ++statistics_.invalid_numeric_groups;
    }
  }

  if ((wire_flags & kLinearAccelerationValid) != 0) {
    for (std::size_t i = 0; i < next.linear_acceleration.size(); ++i) {
      next.linear_acceleration[i] = static_cast<double>(
        read_float32_le(rx_buffer_.data() + 31 + i * 4));
    }
    if (finite_group_or_invalidate(next.linear_acceleration)) {
      next.valid_flags = static_cast<std::uint8_t>(
        next.valid_flags | kLinearAccelerationValid);
    } else {
      ++statistics_.invalid_numeric_groups;
    }
  }

  imu_ = next;
  imu_received_at_ = Clock::now();
  have_imu_frame_ = true;
}

const WheelFeedback & SerialProtocol::wheel_feedback() const noexcept
{
  return wheel_;
}

const ImuFeedback & SerialProtocol::imu_feedback() const noexcept
{
  return imu_;
}

const ProtocolStatistics & SerialProtocol::statistics() const noexcept
{
  return statistics_;
}

bool SerialProtocol::wheel_feedback_usable(const std::chrono::milliseconds timeout) const noexcept
{
  return have_wheel_frame_ && timeout.count() > 0 &&
         wheel_.valid_flags == kAllWheelsValid &&
         (Clock::now() - wheel_received_at_) < timeout;
}

bool SerialProtocol::imu_feedback_usable(
  const std::uint8_t required_flags, const std::chrono::milliseconds timeout) const noexcept
{
  if (!have_imu_frame_ || required_flags == 0 ||
    (required_flags & static_cast<std::uint8_t>(~kAllImuValid)) != 0 ||
    timeout.count() <= 0)
  {
    return false;
  }
  return (imu_.valid_flags & required_flags) == required_flags &&
         (Clock::now() - imu_received_at_) < timeout;
}

}  // namespace x5car
