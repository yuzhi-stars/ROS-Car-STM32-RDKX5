#include "x5car_hardware/safety_checks.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace std::chrono_literals;
using x5car_hardware::ControlLoopWatchdog;
using x5car_hardware::StopConfirmation;
namespace {
void require(bool value, const char * text) {
  if (!value) throw std::runtime_error(text);
}
x5car::WheelFeedback wheel(double speed = 0.0, std::uint8_t flags = 0x0F) {
  return {{{speed, speed, speed, speed}}, flags};
}
}
int main() {
  try {
    const auto start = std::chrono::steady_clock::time_point{} + 1s;
    ControlLoopWatchdog loop;
    require(!loop.read_tick(start), "unarmed watchdog allowed read");
    loop.arm(start);
    for (int tick = 1; tick <= 10; ++tick) {
      require(loop.read_tick(start + tick * 20ms), "normal read rejected");
      require(loop.write_tick(start + tick * 20ms + 1ms), "normal write rejected");
    }
    require(!loop.read_tick(start + 450ms), "250ms pause allowed read");
    loop.arm(start);
    require(loop.read_tick(start + 20ms), "read failed");
    require(!loop.write_tick(start + 120ms), "delay between read and write allowed motion");
    loop.arm(start);
    require(loop.read_tick(start + 50ms), "first read failed");
    require(!loop.read_tick(start + 100ms), "repeated reads concealed missing writes");
    loop.arm(start);
    require(loop.write_tick(start + 50ms), "first write failed");
    require(!loop.write_tick(start + 100ms), "repeated writes concealed missing reads");
    loop.disarm();
    require(!loop.write_tick(start + 101ms), "disarmed watchdog allowed write");
    std::cout << "PASS: normal cycle, pause, delayed write and independent read/write liveness\n";

    StopConfirmation stable(0.05, 200ms);
    stable.observe(wheel(), start);
    require(!stable.confirmed(start + 199ms), "single zero frame confirmed stop");
    require(!stable.confirmed(start + 200ms), "stale zero frame confirmed stop");
    for (int tick = 1; tick <= 9; ++tick) stable.observe(wheel(), start + tick * 20ms);
    require(!stable.confirmed(start + 199ms), "confirmed less than 200ms of samples");
    stable.observe(wheel(), start + 200ms);
    require(stable.confirmed(start + 200ms), "200ms fresh zero samples did not confirm");
    require(!stable.confirmed(start + 400ms), "expired last sample confirmed stop");

    // 相同一次 receive 内夹杂的运动帧也必须重置；最后一帧零不能掩盖它。
    stable.observe(wheel(2), start + 210ms);
    stable.observe(wheel(), start + 210ms);
    require(!stable.confirmed(start + 210ms), "batch [moving,zero] confirmed stop");
    for (int tick = 1; tick <= 10; ++tick) stable.observe(wheel(), start + 210ms + tick * 20ms);
    require(stable.confirmed(start + 410ms), "stop did not recover after 200ms fresh zeros");
    stable.observe(wheel(0, 0x07), start + 420ms);
    require(!stable.confirmed(start + 420ms), "invalid wheel flags confirmed stop");
    stable.observe(wheel(), start + 420ms);
    stable.observe(wheel(), start + 620ms);
    require(!stable.confirmed(start + 620ms), "200ms receive gap counted as stable stop");
    auto invalid = wheel();
    invalid.velocity[2] = std::numeric_limits<double>::quiet_NaN();
    stable.observe(invalid, start + 630ms);
    require(!stable.confirmed(start + 630ms), "NaN confirmed stop");

    StopConfirmation boundary(0.05, 200ms);
    for (int tick = 0; tick <= 10; ++tick) boundary.observe(wheel(-0.05), start + tick * 20ms);
    require(boundary.confirmed(start + 200ms), "threshold boundary rejected");
    boundary.observe(wheel(0.051), start + 210ms);
    require(!boundary.confirmed(start + 210ms), "above-threshold wheel confirmed stop");
    std::cout << "PASS: continuous stop, transient zero, invalid flags, gaps, NaN and threshold\n";
    return 0;
  } catch (const std::exception & error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
