#include "device-ui.h"

#include <Preferences.h>

#include <algorithm>
#include <cmath>

#include "board-config.h"
#include "micro-fonts.h"

namespace {

constexpr std::uint16_t kBackground = 0x0000;
constexpr std::uint16_t kPanel = 0x0883;
constexpr std::uint16_t kPanelPressed = 0x1926;
constexpr std::uint16_t kText = 0xFFFF;
constexpr std::uint16_t kMuted = 0xAD98;
constexpr std::uint16_t kAccent = 0x4CDF;
// Rasterized Segoe UI glyphs with alpha blending for this 320x240 display.
void smoothText(TFT_eSPI& target, const char* text, int x, int y,
                const MicroFont& font, std::uint16_t fg, std::uint16_t bg) {
  for (; *text; ++text) {
    const unsigned char c = (*text >= 32 && *text <= 126) ? *text : '?';
    const MicroGlyph& g = font.glyphs[c - 32];
    for (int row = 0; row < g.h; ++row) {
      for (int col = 0; col < g.w; ++col) {
        const unsigned a = pgm_read_byte(font.pixels + g.offset + row * g.w + col);
        if (!a) continue;
        const unsigned r = (((fg >> 11) & 31) * a + ((bg >> 11) & 31) * (255 - a) + 127) / 255;
        const unsigned green = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (255 - a) + 127) / 255;
        const unsigned b = ((fg & 31) * a + (bg & 31) * (255 - a) + 127) / 255;
        target.drawPixel(x + g.x + col, y + g.y + row, (r << 11) | (green << 5) | b);
      }
    }
    x += g.advance;
  }
}
int smoothWidth(const char* text, const MicroFont& font) {
  int width = 0;
  for (; *text; ++text) width += font.glyphs[(*text >= 32 && *text <= 126 ? *text : '?') - 32].advance;
  return width;
}
std::uint16_t readableStatusColor(StatusKind status) {
  // Backlight brightness is already controlled by the desktop. Do not dim text twice.
  switch (status) {
    case StatusKind::Thinking: return 0x4CDF;
    case StatusKind::Complete: return 0x67CA;
    case StatusKind::Attention: return 0xFEA8;
    case StatusKind::Error: return 0xFB0C;
    default: return kMuted;
  }
}
constexpr std::uint32_t kCalibrationVersion = 2;
struct StoredCalibration {
  std::uint32_t version;
  std::int16_t values[5];
  std::uint16_t check;
};
static_assert(sizeof(StoredCalibration) == 16, "touch calibration record size");
std::uint16_t calibrationCheck(const StoredCalibration& record) {
  std::uint16_t result = 0xA53C;
  for (const auto value : record.values) result ^= static_cast<std::uint16_t>(value);
  return result;
}
constexpr std::uint32_t kOrientationVersion = 1;
constexpr std::uint32_t kNotificationDurationMs = 4000;
constexpr std::uint32_t kAnimationIntervalMs = 90;
constexpr std::int16_t kNavigateStripeHeight = 24;

std::int16_t mapAxis(std::int16_t raw, std::int16_t rawStart, std::int16_t rawEnd,
                     std::int16_t screenStart, std::int16_t screenEnd,
                     std::int16_t screenMaximum) {
  const long denominator = static_cast<long>(rawEnd) - rawStart;
  if (std::abs(denominator) < 100) return screenStart;
  const long value = screenStart +
                     (static_cast<long>(raw) - rawStart) * (screenEnd - screenStart) /
                         denominator;
  return static_cast<std::int16_t>(std::max<long>(0, std::min<long>(screenMaximum, value)));
}

}  // namespace

DeviceUi::DeviceUi()
    : touchBus_(VSPI),
      touch_(board::kTouchChipSelectPin, board::kTouchIrqPin,
             board::kTouchPressureMinimum),
      calibration_{board::kDefaultTouchLeft, board::kDefaultTouchRight,
                   board::kDefaultTouchTop, board::kDefaultTouchBottom,
                   board::kTouchPressureMinimum} {}

void DeviceUi::begin() {
  loadOrientation();
  display_.init();
  backlightPwmReady_ =
      ledcSetup(board::kBacklightPwmChannel, board::kBacklightPwmFrequency,
                board::kBacklightPwmResolution) != 0;
  if (backlightPwmReady_) {
    ledcAttachPin(board::kBacklightPin, board::kBacklightPwmChannel);
  } else {
    pinMode(board::kBacklightPin, OUTPUT);
    Serial.println("UI backlight PWM unavailable; using on/off fallback");
  }
  setBacklight(1.0F, true);
  applyOrientation();
  Serial.printf("UI canvas=%dx%d\n", display_.width(), display_.height());
  display_.setTextWrap(false);
  agentSprite_.setColorDepth(16);
  agentBuffered_ = agentSprite_.createSprite(150, 51) != nullptr;
  Serial.printf("UI smooth cards; buffer=%s\n", agentBuffered_ ? "ready" : "fallback");

  touchBus_.begin(board::kTouchClockPin, board::kTouchMisoPin, board::kTouchMosiPin,
                   board::kTouchChipSelectPin);
  touch_.begin(touchBus_);
  touch_.setRotation(1);
  loadCalibration();
  loadSleep();
  for (auto& status : statuses_) status = StatusKind::Unassigned;
  drawAll();
}

void DeviceUi::loadCalibration() {
  Preferences preferences;
  // 初回起動ではnamespaceがまだ無い。read-writeで開いて空namespaceを正常に作る。
  if (!preferences.begin("codex-touch", false)) return;
  StoredCalibration stored = {};
  if (preferences.getBytesLength("calib") == sizeof(stored) &&
      preferences.getBytes("calib", &stored, sizeof(stored)) == sizeof(stored) &&
      stored.version == kCalibrationVersion &&
      stored.check == calibrationCheck(stored)) {
    TouchCalibration candidate{stored.values[0], stored.values[1],
                               stored.values[2], stored.values[3], stored.values[4]};
    if (std::abs(candidate.right - candidate.left) > 1000 &&
        std::abs(candidate.bottom - candidate.top) > 1000 &&
        candidate.pressure >= 12 && candidate.pressure <= board::kTouchPressureMinimum)
      calibration_ = candidate;
  } else if (preferences.getUInt("version", 0) == 1) {
    TouchCalibration candidate{
        preferences.getShort("left", calibration_.left),
        preferences.getShort("right", calibration_.right),
        preferences.getShort("top", calibration_.top),
        preferences.getShort("bottom", calibration_.bottom),
        board::kTouchPressureMinimum,
    };
    if (std::abs(candidate.right - candidate.left) > 1000 &&
        std::abs(candidate.bottom - candidate.top) > 1000 &&
        candidate.pressure >= 12 && candidate.pressure <= board::kTouchPressureMinimum) {
      calibration_ = candidate;
    }
  }
  preferences.end();
  touch_.setPressureThreshold(calibration_.pressure);
}

bool DeviceUi::saveCalibration() {
  Preferences preferences;
  if (!preferences.begin("codex-touch", false)) return false;
  StoredCalibration stored{kCalibrationVersion,
      {calibration_.left, calibration_.right, calibration_.top,
       calibration_.bottom, calibration_.pressure}, 0};
  stored.check = calibrationCheck(stored);
  const bool saved = preferences.putBytes("calib", &stored, sizeof(stored)) ==
                     sizeof(stored);
  preferences.end();
  return saved;
}

void DeviceUi::loadOrientation() {
  Preferences preferences;
  if (!preferences.begin("codex-ui", false)) return;
  if (preferences.getUInt("version", 0) != kOrientationVersion) {
    preferences.clear();
    preferences.putUInt("version", kOrientationVersion);
    preferences.putBool("inverted", false);
  }
  inverted_ = preferences.getBool("inverted", false);
  preferences.end();
}

void DeviceUi::saveOrientation() {
  Preferences preferences;
  if (!preferences.begin("codex-ui", false)) return;
  preferences.putUInt("version", kOrientationVersion);
  preferences.putBool("inverted", inverted_);
  preferences.end();
}

void DeviceUi::loadSleep() {
  sleepTimeoutSec_ = 0U;
  navigateScroll_ = 0;
  Preferences preferences;
  if (!preferences.begin("codex-ui", true)) return;
  const std::uint32_t saved =
      static_cast<std::uint32_t>(preferences.getUInt("sleep_sec", 0U));
  if (sleep_menu::isValidTimeout(saved)) sleepTimeoutSec_ = saved;
  preferences.end();
}

bool DeviceUi::saveSleep() {
  Preferences preferences;
  if (!preferences.begin("codex-ui", false)) return false;
  preferences.putUInt("sleep_sec", sleepTimeoutSec_);
  preferences.end();
  return true;
}

void DeviceUi::setSleepTimeoutSec(std::uint32_t timeoutSec) {
  if (!sleep_menu::isValidTimeout(timeoutSec)) return;
  if (sleepTimeoutSec_ == timeoutSec) return;
  const std::uint32_t previous = sleepTimeoutSec_;
  sleepTimeoutSec_ = timeoutSec;
  if (!saveSleep()) sleepTimeoutSec_ = previous;
  if (displayAwake_ && page_ == Page::Navigate) refresh();
}

void DeviceUi::moveSliderThumb(std::int16_t centerY, std::uint32_t oldValue,
                               std::uint32_t newValue, std::uint32_t minV,
                               std::uint32_t maxV) {
  using namespace sleep_menu;
  const std::int16_t scroll = clampScroll(navigateScroll_);
  const std::int16_t y =
      static_cast<std::int16_t>(centerY - scroll);
  if (y < kVisibleTop + 8 || y > kVisibleBottom - 8) return;
  const std::int16_t oldX = sliderXFromValue(oldValue, minV, maxV);
  const std::int16_t newX = sliderXFromValue(newValue, minV, maxV);
  display_.startWrite();
  // Erase the old thumb and repaint the track segment beneath it.
  display_.fillRect(oldX - 6, y - 8, 12, 17, kBackground);
  display_.fillRect(oldX - 6, y - 2, 12, 5, kBackground);
  display_.drawRect(kTrackX0, y - 2, kTrackX1 - kTrackX0, 5, kText);
  const std::int16_t fillEnd = sliderXFromValue(newValue, minV, maxV);
  if (fillEnd > kTrackX0)
    display_.fillRect(kTrackX0, y - 2, fillEnd - kTrackX0, 5, kAccent);
  display_.fillRect(newX - 6, y - 6, 12, 13, kText);
  display_.fillRect(newX - 4, y - 4, 8, 9, kPanel);
  display_.endWrite();
}

void DeviceUi::dragSleepSlider(int kind, std::uint32_t newSliderValue) {
  using namespace sleep_menu;
  const std::uint32_t current = sleepTimeoutSec_;
  std::uint32_t oldValue = 0U;
  std::uint32_t total = current;
  std::int16_t centerY = 0;
  std::uint32_t minV = 0U;
  std::uint32_t maxV = 1U;
  if (kind == 1) {
    oldValue = minutesPart(current);
    if (newSliderValue == oldValue) return;
    total = timeoutFromParts(newSliderValue, hoursPart(current));
    centerY = kMinutesY;
    maxV = kMinutesMax;
  } else if (kind == 2) {
    oldValue = hoursPart(current);
    if (newSliderValue == oldValue) return;
    total = timeoutFromParts(minutesPart(current), newSliderValue);
    centerY = kHoursY;
    maxV = kHoursMax;
  } else {
    return;
  }
  if (!isValidTimeout(total)) return;
  sleepTimeoutSec_ = total;
  if (!saveSleep()) {
    sleepTimeoutSec_ = current;
    return;
  }
  if (displayAwake_ && page_ == Page::Navigate)
    moveSliderThumb(centerY, oldValue, newSliderValue, minV, maxV);
}

void DeviceUi::setNavigateScroll(std::int16_t scroll) {
  const std::int16_t clamped = sleep_menu::clampScroll(scroll);
  if (navigateScroll_ == clamped) return;
  navigateScroll_ = clamped;
  if (displayAwake_ && page_ == Page::Navigate) refresh();
}

void DeviceUi::pageNavigateScroll(int dir) {
  if (dir == 0) return;
  setNavigateScroll(
      static_cast<std::int16_t>(navigateScroll_ + dir * 40));
}

void DeviceUi::refresh() {
  if (!displayAwake_) return;
  if (page_ == Page::Navigate) {
    if (!contentSprite_.created()) {
      contentSprite_.setColorDepth(16);
      contentSprite_.setTextWrap(false);
      if (contentSprite_.createSprite(board::kScreenWidth, kNavigateStripeHeight) == nullptr) {
        Serial.println("UI navigate buffer unavailable");
      }
    }
    if (contentSprite_.created()) {
      for (std::int16_t top = sleep_menu::kVisibleTop;
           top < sleep_menu::kVisibleBottom; top += kNavigateStripeHeight) {
        contentSprite_.fillSprite(kBackground);
        drawingTarget_ = &contentSprite_;
        drawingOffsetY_ = top;
        drawNavigate();
        drawingTarget_ = &display_;
        drawingOffsetY_ = 0;
        contentSprite_.pushSprite(0, top, 0, 0, board::kScreenWidth,
                                  std::min<std::int16_t>(kNavigateStripeHeight,
                                      sleep_menu::kVisibleBottom - top));
      }
      return;
    }
  }
  display_.startWrite();
  drawContent();
  display_.endWrite();
}

void DeviceUi::applyOrientation() {
  display_.setRotation(inverted_ ? 0 : 2);
}

bool DeviceUi::captureCalibrationPoint(std::int16_t& rawX, std::int16_t& rawY,
                                       std::int16_t& pressure) {
  const std::uint32_t timeout = millis() + 15000;
  while (static_cast<std::int32_t>(timeout - millis()) > 0) {
    if (touch_.tirqTouched()) {
      std::int32_t sumX = 0;
      std::int32_t sumY = 0;
      std::int16_t samples = 0;
      std::int16_t weakest = 32767;
      while (touch_.tirqTouched() && samples < 12) {
        const SensitiveTouchPoint point = touch_.getPoint();
        if (point.z >= 12) {
          sumX += point.x;
          sumY += point.y;
          weakest = std::min(weakest, point.z);
          ++samples;
        }
        delay(12);
      }
      while (touch_.tirqTouched()) {
        touch_.getPoint();
        delay(10);
      }
      if (samples >= 4) {
        rawX = static_cast<std::int16_t>(sumX / samples);
        rawY = static_cast<std::int16_t>(sumY / samples);
        pressure = weakest;
        return true;
      }
    }
    delay(10);
  }
  return false;
}

void DeviceUi::calibrateTouch() {
  setDisplayAwake(true);
  touch_.setPressureThreshold(12);
  std::int16_t left = 0;
  std::int16_t top = 0;
  std::int16_t right = 0;
  std::int16_t bottom = 0;
  std::int16_t firstPressure = 0;
  std::int16_t secondPressure = 0;

  // Calibration values use the base display orientation (rotation 2).
  display_.setRotation(2);
  const auto finish = [this]() {
    touch_.setPressureThreshold(calibration_.pressure);
    applyOrientation();
    drawAll();
  };

  display_.fillScreen(kBackground);
  display_.setTextColor(kText, kBackground);
  display_.setTextDatum(MC_DATUM);
  display_.drawString("TOUCH 1/2", 160, 120, 2);
  display_.drawLine(14, 24, 34, 24, kAccent);
  display_.drawLine(24, 14, 24, 34, kAccent);
  if (!captureCalibrationPoint(left, top, firstPressure)) {
    display_.fillScreen(kBackground);
    display_.drawString("NO TOUCH", 160, 120, 2);
    delay(1800);
    finish();
    return;
  }

  display_.fillScreen(kBackground);
  display_.drawString("TOUCH 2/2", 160, 120, 2);
  display_.drawLine(285, 215, 305, 215, kAccent);
  display_.drawLine(295, 205, 295, 225, kAccent);
  if (!captureCalibrationPoint(right, bottom, secondPressure)) {
    display_.fillScreen(kBackground);
    display_.drawString("NO TOUCH", 160, 120, 2);
    delay(1800);
    finish();
    return;
  }

  if (std::abs(right - left) > 1000 && std::abs(bottom - top) > 1000) {
    const TouchCalibration previous = calibration_;
    calibration_ = {left, right, top, bottom,
                    touchThresholdForPressure(std::min(firstPressure, secondPressure))};
    if (!saveCalibration()) {
      calibration_ = previous;
      display_.fillScreen(kBackground);
      display_.drawString("SAVE FAILED", 160, 120, 2);
      delay(1800);
    }
  } else {
    display_.fillScreen(kBackground);
    display_.drawString("INVALID TOUCH", 160, 120, 2);
    delay(1800);
  }
  finish();
}

bool DeviceUi::readTouch(std::int16_t& x, std::int16_t& y) {
  if (!touch_.tirqTouched()) {
    touchFilter_.reset();
    return false;
  }
  const SensitiveTouchPoint point = touch_.getPoint();
  if (!touch_.tirqTouched()) {
    touchFilter_.reset();
    return false;
  }
  if (point.z >= calibration_.pressure) {
    const ScreenPoint oriented = orientPoint(
        {mapAxis(point.x, calibration_.left, calibration_.right, 24, 295,
                 board::kScreenWidth - 1),
         mapAxis(point.y, calibration_.top, calibration_.bottom, 24, 215,
                 board::kScreenHeight - 1)},
        inverted_);
    ScreenPoint stabilized;
    touchFilter_.push(oriented, stabilized);
  }
  // push() reports a new tap once; current() follows the same contact.
  ScreenPoint stable;
  if (!touchFilter_.current(stable)) return false;
  x = stable.x;
  y = stable.y;
  return true;
}

void DeviceUi::setDisplayAwake(bool awake) {
  if (displayAwake_ == awake) {
    setBacklight(state_.lightingBrightness, awake);
    return;
  }
  displayAwake_ = awake;
  pressed_ = false;
  touchFilter_.reset();
  if (!awake) {
    setBacklight(state_.lightingBrightness, false);
    display_.writecommand(TFT_DISPOFF);
    display_.writecommand(ILI9341_SLPIN);
    Serial.println("UI display=off");
    return;
  }

  display_.writecommand(ILI9341_SLPOUT);
  delay(120);
  display_.writecommand(TFT_DISPON);
  setBacklight(state_.lightingBrightness, true);
  drawAll();
  Serial.println("UI display=on");
}

void DeviceUi::setBacklight(float brightness, bool awake) {
  const std::uint8_t duty = backlightDuty(brightness, awake);
  if (backlightDuty_ == duty) return;
  backlightDuty_ = duty;
  if (backlightPwmReady_) {
    ledcWrite(board::kBacklightPwmChannel, duty);
  } else {
    digitalWrite(board::kBacklightPin, duty == 0 ? LOW : HIGH);
  }
  Serial.printf("UI backlight duty=%u\n", static_cast<unsigned>(duty));
}

void DeviceUi::setUsbCards(const UsbCards& cards, bool fresh) {
  const bool connectionChanged = usbFresh_ != fresh;
  const bool pageChanged = usbPage_ != cards.page || usbPages_ != cards.pages;
  usbPage_ = cards.page;
  usbPages_ = cards.pages;
  usbFresh_ = fresh;
  for (unsigned i = 0; i < 6; ++i) {
    const bool changed = connectionChanged || strcmp(usbCards_[i].title, cards.cards[i].title) != 0 ||
                         strcmp(usbCards_[i].id, cards.cards[i].id) != 0 ||
                         strcmp(usbCards_[i].subtitle, cards.cards[i].subtitle) != 0;
    usbCards_[i] = cards.cards[i];
    if (changed && displayAwake_ && page_ == Page::Agents) drawAgent(i);
  }
  if (connectionChanged && displayAwake_) drawConnection();
  if (pageChanged && displayAwake_) drawHeader();
}

void DeviceUi::setState(const CodexMicroState& state, std::uint32_t now) {
  const bool connectionChanged = state_.connected != state.connected;
  const std::int8_t previousNotificationAgent = notificationAgent_;
  const StatusKind previousNotificationStatus = notificationStatus_;
  std::array<bool, 6> agentChanged{};
  for (std::size_t index = 0; index < state.threads.size(); ++index) {
    const StatusKind next =
        statusKindForColor(state.threads[index].color, state.threads[index].brightness);
    const ThreadLight& previous = state_.threads[index];
    const ThreadLight& current = state.threads[index];
    agentChanged[index] = statuses_[index] != next || previous.color != current.color ||
                          previous.brightness != current.brightness ||
                          previous.effect != current.effect;
    if (next != statuses_[index] && isNotificationStatus(next)) {
      notificationAgent_ = static_cast<std::int8_t>(index);
      notificationStatus_ = next;
      notificationUntil_ = now + kNotificationDurationMs;
    }
    statuses_[index] = next;
  }
  state_ = state;
  const bool powerChanged = displayAwake_ != state.displayAwake;
  setDisplayAwake(state.displayAwake);
  if (powerChanged || !displayAwake_) return;

  display_.startWrite();
  if (previousNotificationAgent != notificationAgent_ ||
      previousNotificationStatus != notificationStatus_) {
    drawNotification();
  }
  if (connectionChanged) drawConnection();
  if (page_ == Page::Agents) {
    for (std::uint8_t index = 0; index < agentChanged.size(); ++index) {
      if (agentChanged[index]) drawAgent(index);
    }
  }
  display_.endWrite();
}

void DeviceUi::setPage(Page page) {
  if (page_ == page) return;
  const Page previousPage = page_;
  page_ = page;
  pressed_ = false;
  if (!displayAwake_) return;

  // Clear the whole content area (not just the visible band) so scrolled
  // content from the previous page leaves no remnants behind.
  display_.startWrite();
  display_.fillRect(0, 28, 320, 180, kBackground);
  drawContent();
  drawTab(static_cast<std::uint8_t>(previousPage));
  drawTab(static_cast<std::uint8_t>(page_));
  display_.endWrite();
}

void DeviceUi::toggleRotation() {
  inverted_ = !inverted_;
  saveOrientation();
  applyOrientation();
  pressed_ = false;
  if (displayAwake_) drawAll();
  Serial.printf("UI rotation=%s\n", inverted_ ? "inverted" : "normal");
}

void DeviceUi::showPressed(const InputAction& action, bool pressed) {
  if (action.kind == InputKind::None || action.kind == InputKind::PageSwitch) return;
  pressedAction_ = action;
  pressed_ = pressed;
  if (displayAwake_) {
    display_.startWrite();
    drawPressedAction(action);
    display_.endWrite();
  }
  if (!pressed) pressedAction_ = {};
}

void DeviceUi::tick(std::uint32_t now) {
  if (notificationAgent_ >= 0 &&
      static_cast<std::int32_t>(now - notificationUntil_) >= 0) {
    notificationAgent_ = -1;
    if (displayAwake_) {
      display_.startWrite();
      drawNotification();
      display_.endWrite();
    }
  }
  if (displayAwake_ && page_ == Page::Agents &&
      now - lastAnimation_ >= kAnimationIntervalMs) {
    bool transactionStarted = false;
    for (std::uint8_t index = 0; index < state_.threads.size(); ++index) {
      if (state_.threads[index].effect != "breath") continue;
      if (!transactionStarted) {
        display_.startWrite();
        transactionStarted = true;
      }
      drawAgent(index);
    }
    if (transactionStarted) display_.endWrite();
    lastAnimation_ = now;
  }
}

void DeviceUi::drawAll() {
  if (!displayAwake_) return;
  display_.startWrite();
  display_.fillScreen(kBackground);
  drawHeader();
  drawContent();
  drawTabs();
  display_.endWrite();
}

void DeviceUi::drawContent() {
  display_.fillRect(0, 28, 320, 180, kBackground);
  if (page_ == Page::Agents) drawAgents();
  if (page_ == Page::Commands) drawCommands();
  if (page_ == Page::Navigate) {
    display_.setViewport(0, sleep_menu::kVisibleTop, board::kScreenWidth,
                         sleep_menu::kVisibleBottom - sleep_menu::kVisibleTop, false);
    drawNavigate();
    display_.resetViewport();
  }
}

void DeviceUi::drawHeader() {
  display_.fillRect(0, 0, board::kScreenWidth, sleep_menu::kVisibleTop, kPanel);
  display_.setTextDatum(ML_DATUM);
  display_.setTextColor(kText, kPanel);
  char heading[32];
  snprintf(heading, sizeof(heading), "Chats %u/%u", usbPage_, usbPages_);
  smoothText(display_, heading, 8, 1, micro_font_name, kText, kPanel);

  drawNotification();
  drawConnection();
}

void DeviceUi::drawNotification() {
  display_.fillRect(178, 0, 100, sleep_menu::kVisibleTop, kPanel);

  if (notificationAgent_ >= 0) {
    const ThreadLight& light = state_.threads[notificationAgent_];
    const std::uint16_t color = lightColor(light);
    display_.fillRoundRect(182, 3, 90, 22, 5, kPanelPressed);
    display_.setTextDatum(MC_DATUM);
    display_.setTextColor(kText, kPanelPressed);
    char label[8];
    snprintf(label, sizeof(label), "A%d", static_cast<int>(notificationAgent_) + 1);
    display_.drawString(label, 206, 14, 2);
    drawStatusIcon(notificationStatus_, 250, 14, color);
  }
}

void DeviceUi::drawConnection() {
  display_.fillRect(280, 0, 40, sleep_menu::kVisibleTop, kPanel);
  display_.setTextDatum(TL_DATUM);
  display_.setTextColor(usbFresh_ ? 0x07E0 : 0xF800, kPanel);
  display_.drawString("USB", 285, 7, 2);
}

void DeviceUi::drawTabs() {
  for (std::uint8_t index = 0; index < 3; ++index) drawTab(index);
}

void DeviceUi::drawTab(std::uint8_t index) {
  static constexpr const char* labels[] = {"Agents", "Actions", "Navigate"};
  const int x = index * 107;
  const int width = index == 2 ? 106 : 107;
  const bool selected = static_cast<std::uint8_t>(page_) == index;
  display_.fillRect(x, 208, width, 32, kBackground);
  display_.drawFastHLine(x + 8, 209, width - 16, selected ? kAccent : kPanel);
  smoothText(display_, labels[index], x + (width - smoothWidth(labels[index], micro_font_tab)) / 2,
             216, micro_font_tab, selected ? kText : kMuted, kBackground);
}

std::uint16_t DeviceUi::lightColor(const ThreadLight& light, float pulse) {
  const float brightness = std::max(0.0F, std::min(1.0F, light.brightness * pulse));
  const auto red = static_cast<std::uint8_t>(((light.color >> 16U) & 0xFFU) * brightness);
  const auto green = static_cast<std::uint8_t>(((light.color >> 8U) & 0xFFU) * brightness);
  const auto blue = static_cast<std::uint8_t>((light.color & 0xFFU) * brightness);
  return display_.color565(red, green, blue);
}

bool DeviceUi::actionIsPressed(InputKind kind, std::int8_t index) const {
  return pressed_ && pressedAction_.kind == kind &&
         (index < 0 || pressedAction_.index == index);
}

void DeviceUi::drawButton(std::int16_t x, std::int16_t y, std::int16_t width,
                          std::int16_t height, bool pressed, std::uint16_t border) {
  drawingTarget_->fillRoundRect(x, y, width, height, 6,
                                pressed ? kPanelPressed : kPanel);
  drawingTarget_->drawRoundRect(x, y, width, height, 6, border);
}

void DeviceUi::drawAgents() {
  for (std::uint8_t index = 0; index < 6; ++index) drawAgent(index);
}

void DeviceUi::drawAgent(std::uint8_t index) {
  static constexpr const char* labels[] = {"Unknown", "Idle", "Working", "Done", "Needs you", "Error"};
  const int screenX = 6 + (index % 2) * 158;
  const int screenY = 33 + (index / 2) * 57;
  TFT_eSPI& target = agentBuffered_ ? static_cast<TFT_eSPI&>(agentSprite_) : display_;
  const int x = agentBuffered_ ? 0 : screenX;
  const int y = agentBuffered_ ? 0 : screenY;
  if (agentBuffered_) agentSprite_.fillSprite(kBackground);
  const auto status = statuses_[index];
  const bool pressed = actionIsPressed(InputKind::AgentKey, index);
  const std::uint16_t color = readableStatusColor(status);
  // Deep status fills keep white text legible across the whole button.
  static constexpr std::uint16_t fills[] = {0x18E4, 0x18E4, 0x0A50, 0x11E6, 0x72A0, 0x7805};
  const std::uint16_t base = usbFresh_ ? fills[static_cast<unsigned>(status)] : fills[0];
  const std::uint16_t background = pressed ? display_.alphaBlend(70, 0xFFFF, base) : base;
  target.fillRoundRect(x, y, 150, 51, 7, background);
  target.drawRoundRect(x, y, 150, 51, 7, color);
  String title(usbCards_[index].title);
  if (smoothWidth(title.c_str(), micro_font_name) > 130) {
    while (title.length() && smoothWidth((title + "...").c_str(), micro_font_name) > 130)
      title.remove(title.length() - 1);
    title += "...";
  }
  smoothText(target, title.c_str(), x + 10, y - 2, micro_font_name, kText, background);
  String subtitle(usbCards_[index].subtitle);
  if (smoothWidth(subtitle.c_str(), micro_font_detail) > 130) {
    while (subtitle.length() && smoothWidth((subtitle + "...").c_str(), micro_font_detail) > 130)
      subtitle.remove(subtitle.length() - 1);
    subtitle += "...";
  }
  smoothText(target, subtitle.c_str(), x + 10, y + 18, micro_font_detail, kText, background);
  target.fillCircle(x + 14, y + 41, 3, color);
  const char* label = !usbFresh_ ? "Offline" : !usbCards_[index].id[0] ? "No chat" : labels[static_cast<unsigned>(status)];
  smoothText(target, label, x + 23, y + 32,
             micro_font_detail, kText, background);
  if (agentBuffered_) agentSprite_.pushSprite(screenX, screenY);
}

void DeviceUi::drawStatusIcon(StatusKind status, std::int16_t x, std::int16_t y,
                              std::uint16_t color) {
  if (status == StatusKind::Unassigned) {
    display_.drawCircle(x, y, 10, kMuted);
  } else if (status == StatusKind::Idle) {
    display_.drawFastHLine(x - 10, y, 20, color);
  } else if (status == StatusKind::Thinking) {
    display_.fillCircle(x - 9, y, 3, color);
    display_.fillCircle(x, y, 3, color);
    display_.fillCircle(x + 9, y, 3, color);
  } else if (status == StatusKind::Complete) {
    display_.drawLine(x - 10, y, x - 2, y + 8, color);
    display_.drawLine(x - 2, y + 8, x + 12, y - 9, color);
  } else if (status == StatusKind::Attention) {
    display_.drawFastVLine(x, y - 11, 16, color);
    display_.fillCircle(x, y + 10, 2, color);
  } else {
    display_.drawLine(x - 9, y - 9, x + 9, y + 9, color);
    display_.drawLine(x + 9, y - 9, x - 9, y + 9, color);
  }
}

void DeviceUi::drawCommands() {
  for (std::uint8_t index = 0; index < 6; ++index) drawCommand(index);
}

void DeviceUi::drawCommand(std::uint8_t index) {
  const std::int16_t x = 4 + (index % 3) * 106;
  const std::int16_t y = 32 + (index / 3) * 87;
  const bool pressed = actionIsPressed(InputKind::CommandKey, index);
  drawButton(x, y, 100, 80, pressed, index == 1 ? 0x07E0 : kAccent);
  drawCommandIcon(index, x + 50, y + 40, kText);
}

void DeviceUi::drawCommandIcon(std::uint8_t index, std::int16_t x, std::int16_t y,
                               std::uint16_t color) {
  if (index == 0) {
    display_.fillTriangle(x + 3, y - 18, x - 10, y + 2, x, y + 2, color);
    display_.fillTriangle(x - 3, y + 18, x + 10, y - 2, x, y - 2, color);
  } else if (index == 1) {
    display_.drawLine(x - 14, y, x - 3, y + 11, color);
    display_.drawLine(x - 3, y + 11, x + 16, y - 13, color);
  } else if (index == 2) {
    display_.drawLine(x - 13, y - 13, x + 13, y + 13, color);
    display_.drawLine(x + 13, y - 13, x - 13, y + 13, color);
  } else if (index == 3) {
    display_.drawLine(x - 14, y, x - 3, y, color);
    display_.drawLine(x - 3, y, x + 9, y - 12, color);
    display_.drawLine(x - 3, y, x + 9, y + 12, color);
    drawArrow(x + 12, y - 12, 1, 0, color);
    drawArrow(x + 12, y + 12, 1, 0, color);
  } else if (index == 4) {
    display_.fillRoundRect(x - 6, y - 17, 13, 25, 6, color);
    display_.fillRect(x - 14, y - 2, 2, 5, color);
    display_.fillRect(x + 13, y - 2, 2, 5, color);
    display_.drawArc(x, y + 2, 14, 13, 270, 90, color, kPanel, false);
    display_.fillRect(x, y + 16, 2, 6, color);
    display_.fillRect(x - 8, y + 21, 17, 2, color);
  } else {
    display_.fillTriangle(x - 15, y - 13, x + 16, y, x - 15, y + 13, color);
  }
}

void DeviceUi::drawArrow(std::int16_t x, std::int16_t y, std::int8_t dx,
                         std::int8_t dy, std::uint16_t color) {
  const std::int16_t endX = x + dx * 14;
  const std::int16_t endY = y + dy * 14;
  drawingTarget_->drawLine(x - dx * 7, y - dy * 7, endX, endY, color);
  drawingTarget_->drawLine(endX, endY, endX - dx * 7 + dy * 6,
                           endY - dy * 7 - dx * 6, color);
  drawingTarget_->drawLine(endX, endY, endX - dx * 7 - dy * 6,
                           endY - dy * 7 + dx * 6, color);
}

bool DeviceUi::navigateVisible(std::int16_t screenY, std::int16_t height) const {
  if (screenY + height <= sleep_menu::kVisibleTop ||
      screenY >= sleep_menu::kVisibleBottom) return false;
  return drawingTarget_ != &contentSprite_ ||
         (screenY + height > drawingOffsetY_ &&
          screenY < drawingOffsetY_ + kNavigateStripeHeight);
}

void DeviceUi::drawNavigate() {
  using namespace sleep_menu;
  TFT_eSPI& canvas = *drawingTarget_;
  const std::int16_t scroll = clampScroll(navigateScroll_);
  auto visible = [this, scroll](std::int16_t y, std::int16_t h) {
    return navigateVisible(y - scroll, h);
  };
  if (visible(38, 48)) drawJoystickButton(18, 38, 0.75F, 0, -1);
  if (visible(150, 48)) drawJoystickButton(18, 150, 0.25F, 0, 1);
  if (visible(94, 48)) drawJoystickButton(2, 94, 0.50F, -1, 0);
  if (visible(94, 48)) drawJoystickButton(76, 94, 0.00F, 1, 0);
  if (visible(42, 64)) drawEncoderStepButton(0, 150, -1);
  if (visible(42, 64)) drawEncoderStepButton(1, 214, 1);
  if (visible(118, 78)) drawEncoderPressButton();
  const std::uint32_t minutes = minutesPart(sleepTimeoutSec_);
  const std::uint32_t hours = hoursPart(sleepTimeoutSec_);
  canvas.setTextDatum(TL_DATUM);
  auto drawLabel = [&](std::int16_t y, const char* text) {
    if (y - scroll < kVisibleTop || y - scroll > kVisibleBottom - 16) return;
    if (!navigateVisible(y - scroll, 16)) return;
    canvas.setTextColor(kText, kBackground);
    canvas.drawString(text, 8, y - scroll - drawingOffsetY_, 2);
  };
  auto drawTrack = [&](std::int16_t centerY, std::uint32_t value,
                       std::uint32_t minV, std::uint32_t maxV) {
    const std::int16_t screenY =
        static_cast<std::int16_t>(centerY - scroll);
    if (screenY < kVisibleTop + 8 || screenY > kVisibleBottom - 8) return;
    if (!navigateVisible(screenY - 6, 13)) return;
    const std::int16_t y = screenY - drawingOffsetY_;
    canvas.drawRect(kTrackX0, y - 2, kTrackX1 - kTrackX0, 5, kText);
    const std::int16_t thumbX = sliderXFromValue(value, minV, maxV);
    if (thumbX > kTrackX0)
      canvas.fillRect(kTrackX0, y - 2, thumbX - kTrackX0, 5, kAccent);
    canvas.fillRect(thumbX - 6, y - 6, 12, 13, kText);
    canvas.fillRect(thumbX - 4, y - 4, 8, 9, kPanel);
  };
  char line[32];
  if (sleepTimeoutSec_ == 0U) {
    snprintf(line, sizeof(line), "LOCAL SLEEP OFF");
  } else {
    snprintf(line, sizeof(line), "SLEEP %uH %uM", hours, minutes);
  }
  drawLabel(kCombinedY, line);
  snprintf(line, sizeof(line), "MIN 0-59: %u", minutes);
  drawLabel(kMinutesLabelY, line);
  drawTrack(kMinutesY, minutes, 0U, kMinutesMax);
  snprintf(line, sizeof(line), "HRS 0-24: %u", hours);
  drawLabel(kHoursLabelY, line);
  drawTrack(kHoursY, hours, 0U, kHoursMax);
  drawLabel(kNoteY, "0M 0H = LOCAL OFF");
  // Scrollbar stays fixed on the right edge.
  const std::int16_t trackH = kScrollBarY1 - kScrollBarY0;
  const std::int16_t thumbH = static_cast<std::int16_t>(
      static_cast<std::int32_t>(kVisibleBottom - kVisibleTop) * trackH /
      kContentH);
  const std::int16_t travel = trackH - thumbH;
  const std::int16_t thumbY =
      travel <= 0 || kScrollMax <= 0
          ? kScrollBarY0
          : static_cast<std::int16_t>(kScrollBarY0 +
                                      scroll * travel / kScrollMax);
  canvas.drawRect(kScrollBarX0, kScrollBarY0 - drawingOffsetY_, 12, trackH, kPanel);
  canvas.fillRect(kScrollBarX0 + 2, thumbY - drawingOffsetY_, 8, thumbH, kText);
}

void DeviceUi::drawPressedAction(const InputAction& action) {
  if (action.page != page_) return;
  if (action.kind == InputKind::PageSwitch ||
      action.kind == InputKind::SleepMinutes ||
      action.kind == InputKind::SleepHours ||
      action.kind == InputKind::NavigateScroll) {
    return;
  }
  if (action.kind == InputKind::AgentKey && action.index >= 0 && action.index < 6) {
    drawAgent(static_cast<std::uint8_t>(action.index));
  } else if (action.kind == InputKind::CommandKey && action.index >= 0 &&
             action.index < 6) {
    drawCommand(static_cast<std::uint8_t>(action.index));
  } else if (action.kind == InputKind::Joystick) {
    if (action.angle == 0.75F) drawJoystickButton(18, 38, 0.75F, 0, -1);
    if (action.angle == 0.25F) drawJoystickButton(18, 150, 0.25F, 0, 1);
    if (action.angle == 0.50F) drawJoystickButton(2, 94, 0.50F, -1, 0);
    if (action.angle == 0.00F) drawJoystickButton(76, 94, 0.00F, 1, 0);
  } else if (action.kind == InputKind::EncoderStep && action.index >= 0 &&
             action.index < 2) {
    const std::uint8_t index = static_cast<std::uint8_t>(action.index);
    drawEncoderStepButton(index, index == 0 ? 150 : 214, index == 0 ? -1 : 1);
  } else if (action.kind == InputKind::EncoderPress) {
    drawEncoderPressButton();
  }
}

void DeviceUi::drawJoystickButton(std::int16_t x, std::int16_t y, float angle,
                                   std::int8_t dx, std::int8_t dy) {
  const std::int16_t screenY =
      static_cast<std::int16_t>(y - sleep_menu::clampScroll(navigateScroll_));
  if (!navigateVisible(screenY, 48)) return;
  const std::int16_t scrolled = screenY - drawingOffsetY_;
  drawButton(x, scrolled, 70, 48,
             actionIsPressed(InputKind::Joystick) && pressedAction_.angle == angle,
             kAccent);
  drawArrow(x + 35, scrolled + 24, dx, dy, kText);
}

void DeviceUi::drawEncoderStepButton(std::uint8_t index, std::int16_t x,
                                     std::int8_t dx) {
  const std::int16_t screenY =
      static_cast<std::int16_t>(42 - sleep_menu::clampScroll(navigateScroll_));
  if (!navigateVisible(screenY, 64)) return;
  const std::int16_t scrolled = screenY - drawingOffsetY_;
  drawButton(x, scrolled, 60, 64, actionIsPressed(InputKind::EncoderStep, index), 0xFFE0);
  drawArrow(x + 30, scrolled + 32, dx, 0, kText);
}

void DeviceUi::drawEncoderPressButton() {
  const std::int16_t screenY =
      static_cast<std::int16_t>(118 - sleep_menu::clampScroll(navigateScroll_));
  if (!navigateVisible(screenY, 78)) return;
  const std::int16_t scrolled = screenY - drawingOffsetY_;
  drawButton(166, scrolled, 106, 78, actionIsPressed(InputKind::EncoderPress), 0xFFE0);
  drawingTarget_->drawCircle(219, scrolled + 39, 25, kText);
  drawingTarget_->drawCircle(219, scrolled + 39, 16, kMuted);
  drawingTarget_->fillCircle(219, scrolled + 39, 5, kText);
}

// Read-only serial diagnostic; streams actual LCD pixels without sending HID actions.
void DeviceUi::dumpScreen() {
  std::uint8_t row[320 * 3];
  Serial.println("FRAME 320 240 RGB888");
  for (int y = 0; y < 240; ++y) {
    display_.readRectRGB(0, y, 320, 1, row);
    Serial.write(row, sizeof(row));
    delay(1);
  }
  Serial.println("FRAME END");
}
