#pragma once

#include <cstdint>

namespace board {

constexpr std::int16_t kScreenWidth = 320;
constexpr std::int16_t kScreenHeight = 240;

constexpr std::int8_t kBacklightPin = 21;
constexpr std::uint8_t kBacklightPwmChannel = 7;
constexpr std::uint32_t kBacklightPwmFrequency = 5000;
constexpr std::uint8_t kBacklightPwmResolution = 8;
constexpr std::int8_t kBootButtonPin = 0;

constexpr std::int8_t kTouchClockPin = 25;
constexpr std::int8_t kTouchMisoPin = 39;
constexpr std::int8_t kTouchMosiPin = 32;
constexpr std::int8_t kTouchChipSelectPin = 33;
constexpr std::int8_t kTouchIrqPin = 36;
constexpr std::int16_t kTouchPressureMinimum = 120;

// ESP32-2432S028R の代表値。起動時に BOOT を押すと実機値へ再調整できる。
constexpr std::int16_t kDefaultTouchLeft = 200;
constexpr std::int16_t kDefaultTouchRight = 3700;
constexpr std::int16_t kDefaultTouchTop = 240;
constexpr std::int16_t kDefaultTouchBottom = 3800;

}  // namespace board
