#include <Arduino.h>

#include "board-config.h"
#include "codex-micro-ble.h"
#include "device-ui.h"
#include "ui-model.h"

namespace {

CodexMicroBle codex;
DeviceUi ui;
UsbCards usbCards;
bool lastUsbFresh = false;
bool usbChanged = true;
InputAction activeAction;
bool touchActive = false;
bool prevPressed = false;
bool lastConnected = false;
std::uint32_t lastIdleTimeoutSec = 0xFFFFFFFFU;
enum class DragKind : std::uint8_t { None, SleepMinutes, SleepHours, Scroll };
DragKind dragKind = DragKind::None;
std::uint32_t lastDragDrawMs = 0;
std::int16_t dragStartY = 0;
std::int16_t dragStartScroll = 0;
bool bootRawHigh = true;
bool bootStableHigh = true;
bool bootGestureArmed = false;
std::uint32_t bootChangedAt = 0;
std::uint32_t bootPressedAt = 0;

constexpr std::uint32_t kBootDebounceMs = 30;

void press(const InputAction& action) {
  activeAction = action;
  touchActive = true;
  Serial.printf("UI input kind=%u index=%d\n", static_cast<unsigned>(action.kind),
                static_cast<int>(action.index));
  if (action.kind == InputKind::None) return;
  if (action.kind == InputKind::PageSwitch) {
    if (action.page == Page::Agents && ui.page() == Page::Agents && usbCards.fresh(millis()))
      Serial.printf("CYD_PAGE %s\n", usbCards.session);
    ui.setPage(action.page);
    return;
  }
  codex.noteDisplayActivity();
  if (action.kind == InputKind::SleepMinutes ||
      action.kind == InputKind::SleepHours) {
    const std::uint32_t current = ui.sleepTimeoutSec();
    const std::uint32_t minutes =
        action.kind == InputKind::SleepMinutes
            ? static_cast<std::uint32_t>(action.index)
            : sleep_menu::minutesPart(current);
    const std::uint32_t hours =
        action.kind == InputKind::SleepHours
            ? static_cast<std::uint32_t>(action.index)
            : sleep_menu::hoursPart(current);
    ui.setSleepTimeoutSec(sleep_menu::timeoutFromParts(minutes, hours));
    dragKind = action.kind == InputKind::SleepMinutes
                   ? DragKind::SleepMinutes
                   : DragKind::SleepHours;
    return;
  }
  if (action.kind == InputKind::NavigateScroll) {
    return;
  }
  ui.showPressed(action, true);
  if (action.kind == InputKind::AgentKey) {
    if (usbCards.fresh(millis()) && action.index >= 0 && action.index < 6 &&
        usbCards.cards[action.index].id[0]) {
      Serial.printf("CYD_OPEN %s %s\n", usbCards.session, usbCards.cards[action.index].id);
    }
  } else if (action.kind == InputKind::CommandKey) {
    codex.sendKey(protocolKeyFor(action), 1);
  } else if (action.kind == InputKind::EncoderStep) {
    codex.sendKey(protocolKeyFor(action), 2);
  } else if (action.kind == InputKind::EncoderPress) {
    codex.sendKey(protocolKeyFor(action), 1);
  } else if (action.kind == InputKind::Joystick) {
    codex.sendJoystick(action.angle, 1.0F);
  }
}

void wakeDisplay() {
  activeAction = {};
  touchActive = true;
  dragKind = DragKind::None;
  ui.setDisplayAwake(true);
  codex.wakeDisplay();
  codex.noteDisplayActivity();
  Serial.println("UI wake touch consumed");
}

void initializeBootGesture() {
  bootRawHigh = digitalRead(board::kBootButtonPin) == HIGH;
  bootStableHigh = bootRawHigh;
  bootGestureArmed = bootStableHigh;
  bootChangedAt = millis();
  bootPressedAt = 0;
}

void updateBootGesture(std::uint32_t now) {
  const bool rawHigh = digitalRead(board::kBootButtonPin) == HIGH;
  if (rawHigh != bootRawHigh) {
    bootRawHigh = rawHigh;
    bootChangedAt = now;
  }
  if (rawHigh == bootStableHigh || now - bootChangedAt < kBootDebounceMs) return;

  bootStableHigh = rawHigh;
  if (!bootStableHigh) {
    if (bootGestureArmed) bootPressedAt = now;
    return;
  }

  if (bootPressedAt != 0) {
    const BootGesture gesture = bootGestureForDuration(now - bootPressedAt);
    if (gesture == BootGesture::RotateScreen) ui.toggleRotation();
    if (gesture == BootGesture::CalibrateTouch) ui.calibrateTouch();
  }
  bootPressedAt = 0;
  bootGestureArmed = true;
}

void release() {
  if (!touchActive) return;
  if (dragKind != DragKind::None) {
    // Settle the throttled drag position on release.
    ui.refresh();
  }
  dragKind = DragKind::None;
  if (activeAction.kind == InputKind::AgentKey) {
    // USB agent taps have no BLE key-down or key-up counterpart.
  } else if (activeAction.kind == InputKind::CommandKey) {
    codex.sendKey(protocolKeyFor(activeAction), 0);
  } else if (activeAction.kind == InputKind::EncoderPress) {
    codex.sendKey(protocolKeyFor(activeAction), 0);
  } else if (activeAction.kind == InputKind::Joystick) {
    codex.sendJoystick(activeAction.angle, 0.0F);
  }
  if (activeAction.kind != InputKind::None) ui.showPressed(activeAction, false);
  activeAction = {};
  touchActive = false;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(board::kBootButtonPin, INPUT_PULLUP);
  const bool calibrate = digitalRead(board::kBootButtonPin) == LOW;

  ui.begin();
  if (calibrate) ui.calibrateTouch();
  initializeBootGesture();
  codex.begin();
  ui.setState(codex.snapshot(), millis());
  Serial.println("CODEX_CYD_READY");
}

void loop() {
  static char diagnostic[2048] = {};
  static unsigned diagnosticLength = 0;
  static bool overflow = false;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      diagnostic[diagnosticLength] = 0;
      if (!overflow && strcmp(diagnostic, "SHOT") == 0) ui.dumpScreen();
      if (!overflow && strcmp(diagnostic, "CYD_HELLO") == 0)
        Serial.printf("CYD_USB_READY 1 %012llx\n", ESP.getEfuseMac());
      if (!overflow && strncmp(diagnostic, "CYD1 ", 5) == 0 && !touchActive) {
        if (usbCards.accept(diagnostic + 5, millis())) {
          usbChanged = true;
          Serial.printf("CYD_ACK %s\n", usbCards.session);
        } else Serial.println("CYD_REJECT");
      }
      diagnosticLength = 0;
      overflow = false;
    } else if (c != '\r') {
      if (diagnosticLength < sizeof(diagnostic) - 1) diagnostic[diagnosticLength++] = c;
      else overflow = true;
    }
  }
  updateBootGesture(millis());
  std::int16_t x = 0;
  std::int16_t y = 0;
  const bool touched = ui.readTouch(x, y);
  const bool contactActive = ui.touchContactActive();
  const TouchTransition transition = touchTransition(touchActive, touched, contactActive);
  if (transition == TouchTransition::Press) {
    if (ui.displayAwake()) {
      if (x < 178 && y < 28 && usbCards.fresh(millis())) {
        activeAction = {};
        touchActive = true;
        Serial.printf("CYD_PAGE %s\n", usbCards.session);
        return;
      }
      std::int16_t scroll = 0;
      if (ui.page() == Page::Navigate) scroll = ui.navigateScroll();
      const InputAction pressed = actionAt(ui.page(), x, y, scroll);
      press(pressed);
      // Fix the drag gesture mode at press time. Empty taps and scrollbar
      // taps become relative scrolls; slider taps keep adjusting.
      dragKind = DragKind::None;
      if (ui.page() == Page::Navigate &&
          (pressed.kind == InputKind::SleepMinutes ||
           pressed.kind == InputKind::SleepHours ||
           pressed.kind == InputKind::NavigateScroll ||
           pressed.kind == InputKind::None)) {
        dragKind = pressed.kind == InputKind::SleepMinutes
                       ? DragKind::SleepMinutes
                   : pressed.kind == InputKind::SleepHours
                       ? DragKind::SleepHours
                       : DragKind::Scroll;
        dragStartY = y;
        dragStartScroll = ui.navigateScroll();
      }
      if (pressed.kind == InputKind::NavigateScroll) {
        ui.pageNavigateScroll(pressed.index);
      }
    } else {
      wakeDisplay();
    }
  }
  if (transition == TouchTransition::Release) release();
  // Contact-continuation drags on the Navigate tab. Taps are unaffected.
  // Redraws are throttled so a drag does not strobe the screen.
  if (touched && prevPressed && ui.displayAwake() &&
      ui.page() == Page::Navigate && dragKind != DragKind::None &&
      static_cast<std::uint32_t>(millis() - lastDragDrawMs) >= 80U) {
    if (dragKind == DragKind::Scroll) {
      const std::int32_t delta = static_cast<std::int32_t>(dragStartY) - y;
      if (delta < -6 || delta > 6) {
        const std::int16_t target = sleep_menu::clampScroll(
            static_cast<int>(dragStartScroll) + delta);
        if (target != ui.navigateScroll() &&
            (target - ui.navigateScroll() < -5 ||
             target - ui.navigateScroll() > 5)) {
          ui.setNavigateScroll(target);
          lastDragDrawMs = millis();
        }
      }
    } else if (x >= 16 && x <= 283) {
      if (dragKind == DragKind::SleepMinutes) {
        const std::uint32_t minutes = sleep_menu::sliderValueFromX(
            x, 0U, sleep_menu::kMinutesMax, 1U);
        ui.dragSleepSlider(1, minutes);
        lastDragDrawMs = millis();
      } else {
        const std::uint32_t hours = sleep_menu::sliderValueFromX(
            x, 0U, sleep_menu::kHoursMax, 1U);
        ui.dragSleepSlider(2, hours);
        lastDragDrawMs = millis();
      }
    }
  }
  if (!touched) {
    dragKind = DragKind::None;
  }
  prevPressed = touched;
  if (ui.sleepTimeoutSec() != lastIdleTimeoutSec) {
    lastIdleTimeoutSec = ui.sleepTimeoutSec();
    codex.setDisplayIdleTimeout(lastIdleTimeoutSec);
  }
  codex.poll();

  CodexMicroState state = codex.snapshot();
  const auto frameNow = millis();
  const bool fresh = usbCards.fresh(frameNow);
  static std::uint32_t lastAgeDraw = 0;
  const bool ageTick = frameNow - lastAgeDraw >= 1000U;
  if (state.dirty || state.connected != lastConnected || usbChanged || fresh != lastUsbFresh || ageTick) {
    if (ageTick) lastAgeDraw = frameNow;
    lastConnected = state.connected;
    lastUsbFresh = fresh;
    usbChanged = false;
    static constexpr std::uint32_t colors[] = {0, 0xFFFFFF, 0x3388FF, 0x44DD55, 0xFFCC33, 0xFF3333};
    for (unsigned i = 0; i < 6; ++i) {
      state.threads[i].color = !usbCards.stale(i, frameNow) ? colors[usbCards.cards[i].status] : 0;
      state.threads[i].brightness = !usbCards.stale(i, frameNow) && usbCards.cards[i].status ? 1.0F : 0.0F;
      state.threads[i].effect = "solid";
    }
    // USB is the agent display authority; BLE still carries Actions/Navigate.
    state.displayAwake = true;
    state.lightingBrightness = 1.0F;
    ui.setUsbCards(usbCards, fresh, frameNow);
    ui.setState(state, millis());
  }
  ui.tick(millis());
  delay(2);
}
