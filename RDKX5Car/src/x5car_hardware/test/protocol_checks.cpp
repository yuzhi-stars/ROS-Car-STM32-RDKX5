#include "x5car_hardware/x5car_protocol.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

using Protocol = x5car::SerialProtocol;
using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const char * message) {
  if (!condition) throw std::runtime_error(message);
}
Bytes hex(const char * text) {
  std::istringstream input(text);
  Bytes output;
  unsigned int value = 0;
  while (input >> std::hex >> value) output.push_back(static_cast<std::uint8_t>(value));
  return output;
}
void repair_crc(Bytes & frame) {
  const auto crc = Protocol::crc16_ccitt_false(frame.data() + 2, frame.size() - 4);
  frame[frame.size() - 2] = static_cast<std::uint8_t>(crc & 0xffU);
  frame.back() = static_cast<std::uint8_t>(crc >> 8U);
}
void put_float(Bytes & frame, std::size_t offset, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  for (std::size_t i = 0; i < 4; ++i)
    frame[offset + i] = static_cast<std::uint8_t>((bits >> (8U * i)) & 0xffU);
  repair_crc(frame);
}
int main() {
  try {
    const auto zero = hex("AA 55 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 CA F6");
    const auto command = hex("AA 55 03 00 00 80 3F 00 00 00 C0 00 00 40 40 00 00 80 C0 FD 02");
    const auto wheel = hex("AA 55 90 00 00 80 3F 00 00 00 C0 00 00 40 40 00 00 80 C0 0F 3F 39");
    const auto imu = hex(
      "AA 55 91 00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 3F "
      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
      "C3 F5 1C 41 07 24 9A");
    require(zero.size() == 21 && command.size() == 21 &&
      wheel.size() == 22 && imu.size() == 46, "golden frame sizes");
    const char crc_input[] = "123456789";
    require(Protocol::crc16_ccitt_false(
      reinterpret_cast<const std::uint8_t *>(crc_input), 9) == 0x29b1, "CRC standard vector");

    Protocol p;
    require(!p.wheel_feedback_usable() && !p.imu_feedback_usable(), "startup invalid");
    require(std::isnan(p.wheel_feedback().velocity[0]), "startup not fake zero");
    Protocol::SpeedCommandFrame encoded{};
    require(Protocol::encode_speed_command({0, 0, 0, 0}, 10, encoded), "encode stop");
    require(std::equal(encoded.begin(), encoded.end(), zero.begin()), "golden stop bytes");
    require(Protocol::encode_speed_command({1, -2, 3, -4}, 10, encoded), "encode mixed");
    require(std::equal(encoded.begin(), encoded.end(), command.begin()), "golden command bytes");
    require(!Protocol::encode_speed_command({11, 0, 0, 0}, 10, encoded), "reject over-limit");
    require(std::all_of(encoded.begin(), encoded.end(),
      [](std::uint8_t b) { return b == 0; }), "clear failed output");
    require(!Protocol::encode_speed_command(
      {std::numeric_limits<double>::quiet_NaN(), 0, 0, 0}, 10, encoded), "reject NaN command");
    require(!Protocol::encode_speed_command({0, 0, 0, 0}, 0, encoded), "reject invalid limit");
    std::cout << "PASS: CRC, command encoding and invalid commands\n";

    for (std::size_t split = 1; split < wheel.size(); ++split) {
      Protocol fragmented;
      require(fragmented.feed(wheel.data(), split) == 0, "no premature frame");
      require(fragmented.feed(wheel.data() + split, wheel.size() - split) == 1, "all wheel splits");
      require(fragmented.wheel_feedback_usable(), "split wheel valid");
      require(fragmented.wheel_feedback().velocity[1] == -2.0, "wheel ordering");
    }
    for (std::size_t split = 1; split < imu.size(); ++split) {
      Protocol fragmented;
      require(fragmented.feed(imu.data(), split) == 0, "no premature IMU");
      require(fragmented.feed(imu.data() + split, imu.size() - split) == 1, "all IMU splits");
      require(fragmented.imu_feedback_usable(), "split IMU valid");
    }
    p.reset();
    for (const auto byte : wheel) p.feed(&byte, 1);
    require(p.wheel_feedback_usable(), "one-byte feed");
    Bytes combined = command;
    combined.insert(combined.end(), wheel.begin(), wheel.end());
    combined.insert(combined.end(), imu.begin(), imu.end());
    p.reset();
    require(p.feed(combined.data(), combined.size()) == 2, "joined frame count and command echo");
    require(p.statistics().ignored_command_frames == 1, "echo does not become feedback");
    require(std::abs(p.imu_feedback().linear_acceleration[2] - 9.81) < 1e-5, "IMU acceleration");
    std::cout << "PASS: all split points, byte feeds, joined frames and echo\n";

    auto damaged = wheel;
    damaged[5] ^= 1U;
    Bytes noisy{0x12, 0xaa, 0xaa, 0x55, 0x7f, 0x42};
    noisy.insert(noisy.end(), damaged.begin(), damaged.end());
    noisy.insert(noisy.end(), wheel.begin(), wheel.end());
    noisy.insert(noisy.end(), imu.begin(), imu.end());
    p.reset();
    require(p.feed(noisy.data(), noisy.size()) == 2, "recover after noise and bad CRC");
    require(p.statistics().crc_errors > 0 && p.statistics().unknown_types > 0, "error statistics");
    auto embedded = wheel;
    embedded[3] = 0xaa; embedded[4] = 0x55;
    repair_crc(embedded);
    p.reset();
    require(p.feed(embedded.data(), embedded.size()) == 1, "header bytes inside payload");
    require(p.wheel_feedback_usable(), "embedded header not split");
    std::cout << "PASS: noise, overlap, unknown type, CRC recovery and embedded header\n";

    p.reset();
    p.feed(wheel.data(), wheel.size());
    auto invalid_wheels = wheel;
    invalid_wheels[19] = 0;
    repair_crc(invalid_wheels);
    p.feed(invalid_wheels.data(), invalid_wheels.size());
    require(!p.wheel_feedback_usable() &&
      std::isnan(p.wheel_feedback().velocity[0]), "invalid new data replaces valid old data");
    auto nan_wheel = wheel;
    put_float(nan_wheel, 3, std::numeric_limits<float>::quiet_NaN());
    p.feed(nan_wheel.data(), nan_wheel.size());
    require(p.wheel_feedback().valid_flags == 0x0e, "NaN invalidates only affected wheel");

    auto zero_quaternion = imu;
    put_float(zero_quaternion, 15, 0.0F);
    p.feed(zero_quaternion.data(), zero_quaternion.size());
    require(p.imu_feedback().valid_flags == 0x06, "zero quaternion rejected");
    require(p.imu_feedback_usable(Protocol::kAngularVelocityValid), "other IMU groups retained");
    auto near_unit = imu;
    put_float(near_unit, 15, 1.005F);
    p.feed(near_unit.data(), near_unit.size());
    require(p.imu_feedback_usable() &&
      std::abs(p.imu_feedback().orientation[3] - 1.0) < 1e-12, "quaternion normalization");
    auto inf_accel = imu;
    put_float(inf_accel, 39, std::numeric_limits<float>::infinity());
    p.feed(inf_accel.data(), inf_accel.size());
    require(p.imu_feedback().valid_flags == 0x03, "Inf invalidates acceleration group");
    std::cout << "PASS: validity masks, NaN/Inf checks and quaternion validation\n";

    p.reset();
    p.feed(wheel.data(), wheel.size());
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    p.feed(damaged.data(), damaged.size());
    std::this_thread::sleep_for(std::chrono::milliseconds{70});
    require(!p.wheel_feedback_usable(), "bad CRC cannot refresh receive timeout");
    p.reset();
    require(p.statistics().wheel_frames == 0 && !p.wheel_feedback_usable(), "reset clears state");
    p.feed(wheel.data(), 5);
    p.reset();
    require(p.feed(imu.data(), imu.size()) == 1, "reset clears partial frame");
    std::cout << "PASS: 200ms timeout and reset\n";

    std::mt19937 random(0xc0ffee);
    Bytes stress(30000);
    for (auto & byte : stress) byte = static_cast<std::uint8_t>(random() & 0xffU);
    for (int repeat = 0; repeat < 2; ++repeat) {
      stress.insert(stress.end(), wheel.begin(), wheel.end());
      stress.insert(stress.end(), imu.begin(), imu.end());
    }
    p.reset();
    for (std::size_t offset = 0; offset < stress.size();) {
      const auto chunk = std::min<std::size_t>(1 + random() % 37U, stress.size() - offset);
      p.feed(stress.data() + offset, chunk);
      offset += chunk;
    }
    require(p.wheel_feedback_usable() && p.imu_feedback_usable(), "random-stream final resync");
    require(p.wheel_feedback().velocity[3] == -4.0, "final wheel data after stress");
    std::cout << "PASS: 30000-byte deterministic random stream\n";
    std::cout << "ALL PROTOCOL CHECKS PASSED\n";
    return 0;
  } catch (const std::exception & error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}