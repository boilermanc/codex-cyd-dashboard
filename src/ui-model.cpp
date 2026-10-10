#include "ui-model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr std::int16_t kContentBottom = 208;
constexpr std::uint32_t kMinimumBootPressMs = 50;
constexpr std::uint32_t kCalibrationHoldMs = 1500;
constexpr const char* kAgentKeys[] = {"AG00", "AG01", "AG02", "AG03", "AG04", "AG05"};
constexpr const char* kCommandKeys[] = {"ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12"};

bool contains(std::int16_t x, std::int16_t y, std::int16_t left, std::int16_t top,
              std::int16_t width, std::int16_t height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

InputAction gridAction(Page page, std::int16_t x, std::int16_t y, InputKind kind) {
  if (x < 4 || y < 32) {
    return {};
  }
  const auto column = static_cast<std::int16_t>((x - 4) / 106);
  const auto row = static_cast<std::int16_t>((y - 32) / 87);
  if (column < 0 || column > 2 || row < 0 || row > 1) {
    return {};
  }
  const auto left = static_cast<std::int16_t>(4 + column * 106);
  const auto top = static_cast<std::int16_t>(32 + row * 87);
  if (!contains(x, y, left, top, 100, 80)) {
    return {};
  }
  return {kind, page, static_cast<std::int8_t>(row * 3 + column), 0.0F};
}

}  // namespace

void DisplayPowerSync::reset(std::uint32_t nowMs) {
  awake_ = true;
  connected_ = false;
  lightingConfigOff_ = false;
  lastActivityMs_ = nowMs;
  // 接続直後の通常状態が複数回全消灯で届いても画面を即座に消さない。
  ignoreAllOffUntil_ = nowMs + kWakeGraceMs;
  disconnectSleepAt_ = nowMs + kDisconnectSleepMs;
}

bool DisplayPowerSync::observeLightingConfig(bool allOff) {
  lightingConfigOff_ = allOff;
  if (allOff || awake_) return false;
  awake_ = true;
  return true;
}

bool DisplayPowerSync::observeThreadLighting(std::uint8_t updatedMask, bool allOff,
                                             std::uint32_t nowMs) {
  const bool completeAllOff = updatedMask == kAllThreadsMask && allOff;
  if (!completeAllOff) {
    if (awake_) return false;
    awake_ = true;
    return true;
  }
  if (!lightingConfigOff_) return false;
  if (static_cast<std::int32_t>(nowMs - ignoreAllOffUntil_) < 0) {
    return false;
  }
  if (!awake_) return false;
  awake_ = false;
  return true;
}

bool DisplayPowerSync::setConnected(bool connected, std::uint32_t nowMs) {
  connected_ = connected;
  if (!connected) {
    disconnectSleepAt_ = nowMs + kDisconnectSleepMs;
    return false;
  }

  lightingConfigOff_ = false;
  ignoreAllOffUntil_ = nowMs + kWakeGraceMs;
  if (awake_) return false;
  awake_ = true;
  return true;
}

bool DisplayPowerSync::tick(std::uint32_t nowMs) {
  if (!awake_) {
    return false;
  }
  if (!connected_ &&
      static_cast<std::int32_t>(nowMs - disconnectSleepAt_) >= 0) {
    awake_ = false;
    return true;
  }
  if (idleTimeoutSec_ > 0U &&
      static_cast<std::uint32_t>(nowMs - lastActivityMs_) >=
          idleTimeoutSec_ * 1000U) {
    awake_ = false;
    return true;
  }
  return false;
}

bool DisplayPowerSync::wake(std::uint32_t nowMs) {
  const bool changed = !awake_;
  awake_ = true;
  lastActivityMs_ = nowMs;
  // Desktopは活動通知後に現在の照明状態を複数回再送する場合がある。全タスク
  // 未割り当てなら全組が消灯状態なので、短い復帰猶予中はAuto-dim扱いしない。
  ignoreAllOffUntil_ = nowMs + kWakeGraceMs;
  if (!connected_) disconnectSleepAt_ = nowMs + kDisconnectSleepMs;
  return changed;
}

InputAction actionAt(Page page, std::int16_t x, std::int16_t y,
                     std::int16_t scroll) {
  if (x < 0 || x >= 320 || y < 0 || y >= 240) {
    return {};
  }

  if (y >= kContentBottom) {
    const auto tab = static_cast<std::uint8_t>(std::min<std::int16_t>(2, x / 107));
    return {InputKind::PageSwitch, static_cast<Page>(tab), -1, 0.0F};
  }

  if (page == Page::Agents) {
    if (x < 6 || y < 33) return {};
    const int column = (x - 6) / 158;
    const int row = (y - 33) / 57;
    if (column > 1 || row > 2 ||
        !contains(x, y, 6 + column * 158, 33 + row * 57, 150, 51)) return {};
    return {InputKind::AgentKey, page, static_cast<std::int8_t>(row * 2 + column), 0.0F};
  }
  if (page == Page::Commands) {
    return gridAction(page, x, y, InputKind::CommandKey);
  }

  return navigateActionAt(x, y, scroll);
}

InputAction navigateActionAt(std::int16_t x, std::int16_t y,
                               std::int16_t scroll) {
  using namespace sleep_menu;
  const Page page = Page::Navigate;
  const std::int16_t contentY =
      static_cast<std::int16_t>(y + clampScroll(scroll));
  // Scrollbar wins over the encoder area it overlaps. Taps page the
  // content; drags track the thumb proportionally (see main loop).
  if (x >= kScrollBarX0 - 2 && y >= kScrollBarY0 && y < kScrollBarY1) {
    const std::int16_t trackH = kScrollBarY1 - kScrollBarY0;
    const std::int16_t thumbH = static_cast<std::int16_t>(
        static_cast<std::int32_t>(kVisibleBottom - kVisibleTop) * trackH /
        kContentH);
    const std::int16_t travel = trackH - thumbH;
    const std::int16_t clamped = clampScroll(scroll);
    const std::int16_t thumbY =
        travel <= 0 || kScrollMax <= 0
            ? kScrollBarY0
            : static_cast<std::int16_t>(kScrollBarY0 +
                                        clamped * travel / kScrollMax);
    const std::int8_t dir =
        y < thumbY ? -1 : (y >= thumbY + thumbH ? 1 : 0);
    return {InputKind::NavigateScroll, page, dir, 0.0F};
  }
  if (contains(x, contentY, 18, 38, 70, 48))
    return {InputKind::Joystick, page, -1, 0.75F};
  if (contains(x, contentY, 18, 150, 70, 48))
    return {InputKind::Joystick, page, -1, 0.25F};
  if (contains(x, contentY, 2, 94, 70, 48))
    return {InputKind::Joystick, page, -1, 0.50F};
  if (contains(x, contentY, 76, 94, 70, 48))
    return {InputKind::Joystick, page, -1, 0.00F};
  if (contains(x, contentY, 150, 42, 60, 64))
    return {InputKind::EncoderStep, page, 0, 0.0F};
  if (contains(x, contentY, 214, 42, 60, 64))
    return {InputKind::EncoderStep, page, 1, 0.0F};
  if (contains(x, contentY, 166, 118, 106, 78))
    return {InputKind::EncoderPress, page, -1, 0.0F};
  if (x >= 16 && x <= 283) {
    if (contentY >= kMinutesY - kHalfH && contentY < kMinutesY + kHalfH)
      return {InputKind::SleepMinutes, page,
              static_cast<std::int8_t>(sliderValueFromX(x, 0U, kMinutesMax, 1U)),
              0.0F};
    if (contentY >= kHoursY - kHalfH && contentY < kHoursY + kHalfH)
      return {InputKind::SleepHours, page,
              static_cast<std::int8_t>(sliderValueFromX(x, 0U, kHoursMax, 1U)),
              0.0F};
  }
  return {};
}

ScreenPoint orientPoint(ScreenPoint point, bool inverted) {
  if (!inverted) return point;
  return {static_cast<std::int16_t>(319 - point.x),
          static_cast<std::int16_t>(239 - point.y)};
}

bool TouchSampleFilter::push(ScreenPoint sample, ScreenPoint& stabilized) {
  if (delivered_) {
    // Keep tracking the finger for drags with a bounded moving average.
    // The tap itself stays one-shot. A plain accumulator would overflow
    // count_ and divide by zero on long holds.
    emaX_ += (static_cast<std::int32_t>(sample.x) - emaX_) / 4;
    emaY_ += (static_cast<std::int32_t>(sample.y) - emaY_) / 4;
    stabilized = {static_cast<std::int16_t>(emaX_),
                  static_cast<std::int16_t>(emaY_)};
    last_ = stabilized;
    return false;
  }
  if (count_ > 0) {
    const auto averageX = static_cast<std::int16_t>(sumX_ / count_);
    const auto averageY = static_cast<std::int16_t>(sumY_ / count_);
    if (std::abs(sample.x - averageX) > kMaximumDelta ||
        std::abs(sample.y - averageY) > kMaximumDelta) {
      reset();
    }
  }

  sumX_ += sample.x;
  sumY_ += sample.y;
  ++count_;
  if (count_ < kRequiredSamples) return false;

  stabilized = {static_cast<std::int16_t>(sumX_ / count_),
                static_cast<std::int16_t>(sumY_ / count_)};
  last_ = stabilized;
  emaX_ = stabilized.x;
  emaY_ = stabilized.y;
  delivered_ = true;
  return true;
}

void TouchSampleFilter::reset() {
  sumX_ = 0;
  sumY_ = 0;
  count_ = 0;
  delivered_ = false;
  emaX_ = 0;
  emaY_ = 0;
}

BootGesture bootGestureForDuration(std::uint32_t durationMs) {
  if (durationMs < kMinimumBootPressMs) return BootGesture::None;
  if (durationMs >= kCalibrationHoldMs) return BootGesture::CalibrateTouch;
  return BootGesture::RotateScreen;
}

std::int16_t touchThresholdForPressure(std::int16_t weakestPressure) {
  return std::max<std::int16_t>(12, std::min<std::int16_t>(120, weakestPressure / 2));
}

TouchTransition touchTransition(bool captured, bool hasPoint, bool contactActive) {
  if (!captured && hasPoint) return TouchTransition::Press;
  if (captured && !contactActive) return TouchTransition::Release;
  return TouchTransition::None;
}

const char* protocolKeyFor(const InputAction& action) {
  if (action.kind == InputKind::AgentKey && action.index >= 0 && action.index < 6) {
    return kAgentKeys[action.index];
  }
  if (action.kind == InputKind::CommandKey && action.index >= 0 && action.index < 6) {
    return kCommandKeys[action.index];
  }
  if (action.kind == InputKind::EncoderStep && action.index == 0) return "ENC_CW";
  if (action.kind == InputKind::EncoderStep && action.index == 1) return "ENC_CC";
  if (action.kind == InputKind::EncoderPress) return "ENC_CLK";
  return nullptr;
}

StatusKind statusKindForColor(std::uint32_t rgb, float brightness) {
  if (brightness <= 0.01F) {
    return StatusKind::Unassigned;
  }

  const auto red = static_cast<int>((rgb >> 16U) & 0xFFU);
  const auto green = static_cast<int>((rgb >> 8U) & 0xFFU);
  const auto blue = static_cast<int>(rgb & 0xFFU);
  const int maximum = std::max(red, std::max(green, blue));
  const int minimum = std::min(red, std::min(green, blue));

  if (maximum - minimum < 45) return StatusKind::Idle;
  if (blue > red + 35 && blue > green + 15) return StatusKind::Thinking;
  if (red > 150 && green > 70 && green < red && blue < green) return StatusKind::Attention;
  if (red > green + 35 && red > blue + 35) return StatusKind::Error;
  if (green > red + 20 && green >= blue - 10) return StatusKind::Complete;
  return StatusKind::Idle;
}

bool isNotificationStatus(StatusKind status) {
  return status == StatusKind::Complete || status == StatusKind::Attention ||
         status == StatusKind::Error;
}

float synchronizedLightingBrightness(float previous, float observed,
                                     bool observable) {
  const float safePrevious = std::isfinite(previous)
                                 ? std::max(0.0F, std::min(1.0F, previous))
                                 : 1.0F;
  if (!observable || !std::isfinite(observed)) return safePrevious;
  return std::max(0.0F, std::min(1.0F, observed));
}

std::uint8_t backlightDuty(float brightness, bool displayAwake) {
  if (!displayAwake || !std::isfinite(brightness)) return 0;
  const float clamped = std::max(0.0F, std::min(1.0F, brightness));
  return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
}
