#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLECharacteristic.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>

#include <array>

#include "ui-model.h"

struct ThreadLight {
  std::uint32_t color = 0;
  float brightness = 0.0F;
  String effect = "off";
  float speed = 0.0F;
};

struct LightingSide {
  std::uint32_t color = 0;
  float brightness = 0.0F;
  String effect = "off";
  float speed = 0.0F;
};

struct CodexMicroState {
  std::array<ThreadLight, 6> threads;
  LightingSide ambient;
  LightingSide keys;
  float lightingBrightness = 1.0F;
  bool connected = false;
  bool displayAwake = true;
  bool dirty = true;
};

class CodexMicroBle {
 public:
  static constexpr std::uint16_t kVendorId = 0x303A;
  static constexpr std::uint16_t kProductId = 0x8360;
  static constexpr std::uint8_t kReportId = 6;

  void begin();
  void poll();
  void sendKey(const char* key, std::uint8_t action, std::int8_t agent = -1);
  void sendJoystick(float angle, float distance);
  void wakeDisplay();
  void noteDisplayActivity();
  void setDisplayIdleTimeout(std::uint32_t timeoutSec);
  CodexMicroState snapshot();

 private:
  class ServerCallbacks;
  class InputCallbacks;
  class OutputCallbacks;

  struct QueuedMessage {
    std::uint16_t length = 0;
    char payload[512] = {};
  };

  void onConnected(bool connected);
  void onOutput(const std::uint8_t* data, std::size_t length);
  void handleRpc(const JsonDocument& request);
  void sendResult(JsonVariantConst id, JsonVariantConst result);
  void sendSuccess(JsonVariantConst id);
  void sendJson(const String& json);
  void flushJson(const QueuedMessage& message);
  void updateThreadLighting(JsonArrayConst values);
  static void updateLightingSide(LightingSide& side, JsonObjectConst value);

  BLEHIDDevice* hid_ = nullptr;
  BLECharacteristic* input_ = nullptr;
  BLECharacteristic* output_ = nullptr;
  SemaphoreHandle_t stateMutex_ = nullptr;
  QueueHandle_t txQueue_ = nullptr;
  CodexMicroState state_;
  DisplayPowerSync displayPower_;
  String rpcBuffer_;
};
