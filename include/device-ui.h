#pragma once

#include <SPI.h>
#include <TFT_eSPI.h>
#include <sensitive-xpt2046.h>

#include <array>
#include <cstdint>

#include "codex-micro-ble.h"
#include "ui-model.h"
#include "usb-cards.h"

class DeviceUi {
 public:
  DeviceUi();

  void begin();
  void dumpScreen();
  void calibrateTouch();
  bool readTouch(std::int16_t& x, std::int16_t& y);
  bool touchContactActive() const { return touch_.tirqTouched(); }
  void setDisplayAwake(bool awake);
  bool displayAwake() const { return displayAwake_; }
  void setState(const CodexMicroState& state, std::uint32_t now);
  void setUsbCards(const UsbCards& cards, bool fresh);
  void setPage(Page page);
  void toggleRotation();
  Page page() const { return page_; }
  std::uint32_t sleepTimeoutSec() const { return sleepTimeoutSec_; }
  void setSleepTimeoutSec(std::uint32_t timeoutSec);
  void dragSleepSlider(int kind, std::uint32_t newSliderValue);
  std::int16_t navigateScroll() const { return navigateScroll_; }
  void setNavigateScroll(std::int16_t scroll);
  void pageNavigateScroll(int dir);
  void refresh();
  void showPressed(const InputAction& action, bool pressed);
  void tick(std::uint32_t now);

 private:
  struct TouchCalibration {
    std::int16_t left;
    std::int16_t right;
    std::int16_t top;
    std::int16_t bottom;
    std::int16_t pressure;
  };

  void loadCalibration();
  bool saveCalibration();
  void loadSleep();
  bool saveSleep();
  void loadOrientation();
  void saveOrientation();
  void applyOrientation();
  bool captureCalibrationPoint(std::int16_t& rawX, std::int16_t& rawY,
                               std::int16_t& pressure);
  void drawAll();
  void drawContent();
  void drawHeader();
  void drawNotification();
  void drawConnection();
  void drawTabs();
  void drawTab(std::uint8_t index);
  void drawAgents();
  void drawAgent(std::uint8_t index);
  void drawCommands();
  void drawCommand(std::uint8_t index);
  void drawNavigate();
  bool navigateVisible(std::int16_t screenY, std::int16_t height) const;
  void drawPressedAction(const InputAction& action);
  void drawJoystickButton(std::int16_t x, std::int16_t y, float angle,
                          std::int8_t dx, std::int8_t dy);
  void drawEncoderStepButton(std::uint8_t index, std::int16_t x,
                             std::int8_t dx);
  void drawEncoderPressButton();
  void drawButton(std::int16_t x, std::int16_t y, std::int16_t width,
                    std::int16_t height, bool pressed, std::uint16_t border);
  void moveSliderThumb(std::int16_t centerY, std::uint32_t oldValue,
                       std::uint32_t newValue, std::uint32_t minV,
                       std::uint32_t maxV);
  void drawStatusIcon(StatusKind status, std::int16_t x, std::int16_t y,
                      std::uint16_t color);
  void drawCommandIcon(std::uint8_t index, std::int16_t x, std::int16_t y,
                       std::uint16_t color);
  void drawArrow(std::int16_t x, std::int16_t y, std::int8_t dx, std::int8_t dy,
                 std::uint16_t color);
  void setBacklight(float brightness, bool awake);
  std::uint16_t lightColor(const ThreadLight& light, float pulse = 1.0F);
  bool actionIsPressed(InputKind kind, std::int8_t index = -1) const;

  TFT_eSPI display_{320, 240};
  TFT_eSprite contentSprite_{&display_};
  TFT_eSprite agentSprite_{&display_};
  bool agentBuffered_ = false;
  TFT_eSPI* drawingTarget_ = &display_;
  std::int16_t drawingOffsetY_ = 0;
  SPIClass touchBus_;
  SensitiveXpt2046 touch_;
  TouchCalibration calibration_;
  CodexMicroState state_;
  std::array<UsbCard, 6> usbCards_{};
  bool usbFresh_ = false;
  unsigned usbPage_ = 1;
  unsigned usbPages_ = 1;
  std::array<StatusKind, 6> statuses_{};
  Page page_ = Page::Agents;
  std::uint32_t sleepTimeoutSec_ = 0U;
  std::int16_t navigateScroll_ = 0;
  InputAction pressedAction_;
  TouchSampleFilter touchFilter_;
  bool pressed_ = false;
  bool inverted_ = false;
  bool displayAwake_ = true;
  bool backlightPwmReady_ = false;
  std::uint8_t backlightDuty_ = 0;
  std::int8_t notificationAgent_ = -1;
  StatusKind notificationStatus_ = StatusKind::Unassigned;
  std::uint32_t notificationUntil_ = 0;
  std::uint32_t lastAnimation_ = 0;
};
