// Protocol transport adapted from ZyoungInc/codex-keyboard (MIT), commit
// 2ee23a4ab696f94bb78d250f28cc4a9b879ba079. See NOTICE.md.

#include "codex-micro-ble.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr char kDeviceName[] = "Codex Micro";
constexpr char kManufacturer[] = "Work Louder";
constexpr char kFirmwareVersion[] = "0.4.1";
constexpr std::size_t kPayloadSize = 61;
constexpr std::size_t kReportBodySize = 63;
constexpr std::size_t kMaximumRpcBytes = 4096;
constexpr std::size_t kTransmitQueueDepth = 8;
constexpr std::uint8_t kAllThreadsMask = 0x3F;

constexpr std::uint16_t swapBytes(std::uint16_t value) {
  return static_cast<std::uint16_t>((value << 8U) | (value >> 8U));
}

bool isOffEffect(JsonVariantConst effect) {
  if (effect.is<const char*>()) return std::strcmp(effect.as<const char*>(), "off") == 0;
  return effect.is<int>() && effect.as<int>() == 0;
}

bool isLightingValueOff(JsonObjectConst value) {
  if (value.isNull() || value["c"].isNull() || value["b"].isNull() ||
      value["e"].isNull()) {
    return false;
  }
  return value["c"].as<std::uint32_t>() == 0 && value["b"].as<float>() <= 0.01F &&
         isOffEffect(value["e"]);
}

bool isLightingConfigOff(JsonObjectConst config) {
  return isLightingValueOff(config["ambient"].as<JsonObjectConst>()) &&
         isLightingValueOff(config["keys"].as<JsonObjectConst>());
}

float observedBrightness(JsonObjectConst value) {
  if (value.isNull() || value["b"].isNull()) return 0.0F;
  return std::max(0.0F, value["b"].as<float>());
}

bool hasObservableBrightness(JsonObjectConst value) {
  if (value.isNull() || value["b"].isNull() || value["c"].isNull() ||
      value["e"].isNull()) {
    return false;
  }
  return value["c"].as<std::uint32_t>() != 0 || !isOffEffect(value["e"]);
}

float observedBrightness(JsonArrayConst values) {
  float maximum = 0.0F;
  for (JsonObjectConst value : values) {
    maximum = std::max(maximum, observedBrightness(value));
  }
  return maximum;
}

bool hasObservableBrightness(JsonArrayConst values) {
  for (JsonObjectConst value : values) {
    if (hasObservableBrightness(value)) return true;
  }
  return false;
}

const std::uint8_t kReportMap[] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x06, 0x15, 0x00,
    0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F, 0x09, 0x01, 0x81, 0x02,
    0x95, 0x3F, 0x09, 0x02, 0x91, 0x02, 0xC0,
};

class SecurityCallbacks final : public BLESecurityCallbacks {
 public:
  bool onSecurityRequest() override { return true; }
  std::uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(std::uint32_t) override {}
  bool onConfirmPIN(std::uint32_t) override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    Serial.printf("BLE pairing %s\n", result.success ? "complete" : "failed");
  }
};

}  // namespace

class CodexMicroBle::ServerCallbacks final : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(CodexMicroBle& owner) : owner_(owner) {}

  void onConnect(BLEServer*) override { owner_.onConnected(true); }
  void onDisconnect(BLEServer*) override {
    owner_.onConnected(false);
    BLEDevice::startAdvertising();
  }

 private:
  CodexMicroBle& owner_;
};

class CodexMicroBle::OutputCallbacks final : public BLECharacteristicCallbacks {
 public:
  explicit OutputCallbacks(CodexMicroBle& owner) : owner_(owner) {}

  void onWrite(BLECharacteristic* characteristic) override {
    const std::string value = characteristic->getValue();
    owner_.onOutput(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
  }

 private:
  CodexMicroBle& owner_;
};

class CodexMicroBle::InputCallbacks final : public BLECharacteristicCallbacks {
 public:
  void onStatus(BLECharacteristic*, Status status, std::uint32_t code) override {
    if (!reportedSuccess_ || status != Status::SUCCESS_NOTIFY) {
      Serial.printf("BLE notify status=%d code=%lu\n", static_cast<int>(status),
                    static_cast<unsigned long>(code));
    }
    reportedSuccess_ = reportedSuccess_ || status == Status::SUCCESS_NOTIFY;
  }

 private:
  bool reportedSuccess_ = false;
};

void CodexMicroBle::begin() {
  stateMutex_ = xSemaphoreCreateMutex();
  txQueue_ = xQueueCreate(kTransmitQueueDepth, sizeof(QueuedMessage));
  if (stateMutex_ == nullptr || txQueue_ == nullptr) {
    Serial.println("BLE transport allocation failed");
    return;
  }
  displayPower_.reset(millis());

  BLEDevice::init(kDeviceName);
  // Report IDを除く63 bytesのnotificationが収まるATT MTUを確保する。
  BLEDevice::setMTU(185);
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
  auto* security = new BLESecurity();
  security->setCapability(ESP_IO_CAP_NONE);
  security->setAuthenticationMode(ESP_LE_AUTH_BOND);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks(*this));
  hid_ = new BLEHIDDevice(server);
  hid_->manufacturer()->setValue(kManufacturer);
  hid_->pnp(0x02, swapBytes(kVendorId), swapBytes(kProductId), swapBytes(0x0101));
  hid_->hidInfo(0x00, 0x01);
  hid_->reportMap(const_cast<std::uint8_t*>(kReportMap), sizeof(kReportMap));

  input_ = hid_->inputReport(kReportId);
  input_->setCallbacks(new InputCallbacks());
  auto* inputCccd = static_cast<BLE2902*>(
      input_->getDescriptorByUUID(BLEUUID(static_cast<std::uint16_t>(0x2902))));
  if (inputCccd != nullptr) inputCccd->setNotifications(true);
  output_ = hid_->outputReport(kReportId);
  output_->setCallbacks(new OutputCallbacks(*this));
  hid_->startServices();
  hid_->setBatteryLevel(100);

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->setAppearance(GENERIC_HID);
  advertising->addServiceUUID(hid_->hidService()->getUUID());
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.printf("BLE HID ready VID=%04X PID=%04X report=%u\n", kVendorId, kProductId,
                kReportId);
}

void CodexMicroBle::poll() {
  if (stateMutex_ != nullptr) {
    xSemaphoreTake(stateMutex_, portMAX_DELAY);
    if (displayPower_.tick(millis())) {
      state_.displayAwake = false;
      state_.dirty = true;
    }
    xSemaphoreGive(stateMutex_);
  }
  if (txQueue_ == nullptr) return;
  QueuedMessage message;
  while (xQueueReceive(txQueue_, &message, 0) == pdTRUE) flushJson(message);
}

void CodexMicroBle::sendKey(const char* key, std::uint8_t action, std::int8_t agent) {
  StaticJsonDocument<192> message;
  message["method"] = "v.oai.hid";
  JsonObject params = message.createNestedObject("params");
  params["k"] = key;
  params["act"] = action;
  if (agent >= 0) params["ag"] = agent;

  String json;
  serializeJson(message, json);
  sendJson(json);
}

void CodexMicroBle::sendJoystick(float angle, float distance) {
  StaticJsonDocument<160> message;
  message["method"] = "v.oai.rad";
  JsonObject params = message.createNestedObject("params");
  params["a"] = angle;
  params["d"] = distance;

  String json;
  serializeJson(message, json);
  sendJson(json);
}

void CodexMicroBle::wakeDisplay() {
  if (stateMutex_ == nullptr) return;
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  displayPower_.wake(millis());
  state_.displayAwake = true;
  state_.dirty = true;
  xSemaphoreGive(stateMutex_);

  // Desktopは任意のHID通知を照明アクティビティとして扱う。未割り当ての
  // キーIDを使い、復帰タッチでアプリ操作を発生させず照明状態だけ再送させる。
  sendKey("__WAKE__", 2);
}

void CodexMicroBle::noteDisplayActivity() {
  if (stateMutex_ == nullptr) return;
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  displayPower_.noteActivity(millis());
  xSemaphoreGive(stateMutex_);
}

void CodexMicroBle::setDisplayIdleTimeout(std::uint32_t timeoutSec) {
  if (stateMutex_ == nullptr) return;
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  displayPower_.setIdleTimeoutSec(timeoutSec);
  xSemaphoreGive(stateMutex_);
}

CodexMicroState CodexMicroBle::snapshot() {
  CodexMicroState copy;
  if (stateMutex_ == nullptr) return copy;
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  copy = state_;
  state_.dirty = false;
  xSemaphoreGive(stateMutex_);
  return copy;
}

void CodexMicroBle::onConnected(bool connected) {
  if (stateMutex_ == nullptr) return;
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  displayPower_.setConnected(connected, millis());
  state_.connected = connected;
  state_.displayAwake = displayPower_.awake();
  state_.dirty = true;
  xSemaphoreGive(stateMutex_);
  rpcBuffer_.clear();
  if (!connected && txQueue_ != nullptr) xQueueReset(txQueue_);
  Serial.printf("BLE host %s\n", connected ? "connected" : "disconnected");
}

void CodexMicroBle::onOutput(const std::uint8_t* data, std::size_t length) {
  if (data == nullptr || length < 2) return;

  const std::size_t offset = (length >= 3 && data[0] == kReportId) ? 1 : 0;
  if (length < offset + 2 || data[offset] != 2) return;
  const std::size_t payloadLength = std::min<std::size_t>(data[offset + 1], kPayloadSize);
  if (length < offset + 2 + payloadLength) return;

  const char* payload = reinterpret_cast<const char*>(data + offset + 2);
  constexpr char kTopLevelPrefix[] = "{\"method\"";
  const bool startsTopLevel = payloadLength >= sizeof(kTopLevelPrefix) - 1 &&
                              std::memcmp(payload, kTopLevelPrefix,
                                          sizeof(kTopLevelPrefix) - 1) == 0;
  if (startsTopLevel && !rpcBuffer_.isEmpty()) rpcBuffer_.clear();

  if (rpcBuffer_.length() + payloadLength > kMaximumRpcBytes) {
    Serial.println("RPC input exceeded limit");
    rpcBuffer_.clear();
    return;
  }

  if (rpcBuffer_.isEmpty()) {
    std::size_t jsonStart = 0;
    while (jsonStart < payloadLength && payload[jsonStart] != '{') ++jsonStart;
    if (jsonStart == payloadLength) return;
    rpcBuffer_.concat(payload + jsonStart, payloadLength - jsonStart);
  } else {
    rpcBuffer_.concat(payload, payloadLength);
  }

  DynamicJsonDocument request(kMaximumRpcBytes);
  const DeserializationError error = deserializeJson(request, rpcBuffer_);
  if (error == DeserializationError::IncompleteInput) return;
  if (error) {
    Serial.printf("RPC parse error: %s\n", error.c_str());
    rpcBuffer_.clear();
    return;
  }

  handleRpc(request);
  rpcBuffer_.clear();
}

void CodexMicroBle::handleRpc(const JsonDocument& request) {
  const char* method = request["method"] | "";
  const JsonVariantConst id = request["id"];
  const JsonVariantConst params = request["params"];
  Serial.printf("RPC method=%s id=%d\n", method, id.is<int>() ? id.as<int>() : -1);

  if (std::strcmp(method, "sys.version") == 0) {
    StaticJsonDocument<128> result;
    result["version"] = kFirmwareVersion;
    sendResult(id, result.as<JsonVariantConst>());
    return;
  }
  if (std::strcmp(method, "device.status") == 0) {
    StaticJsonDocument<256> result;
    result["version"] = kFirmwareVersion;
    result["profile_index"] = 0;
    result["layer_index"] = 1;
    result["battery"] = 100;
    result["is_charging"] = false;
    sendResult(id, result.as<JsonVariantConst>());
    return;
  }
  if (std::strcmp(method, "v.oai.thstatus") == 0 && params.is<JsonArrayConst>()) {
    updateThreadLighting(params.as<JsonArrayConst>());
    sendSuccess(id);
    return;
  }
  if (std::strcmp(method, "v.oai.rgbcfg") == 0 && params.is<JsonObjectConst>()) {
    const JsonObjectConst config = params.as<JsonObjectConst>();
    const bool allOff = isLightingConfigOff(config);
    const float observed =
        std::max(observedBrightness(config["ambient"].as<JsonObjectConst>()),
                 observedBrightness(config["keys"].as<JsonObjectConst>()));
    const bool brightnessObservable =
        hasObservableBrightness(config["ambient"].as<JsonObjectConst>()) ||
        hasObservableBrightness(config["keys"].as<JsonObjectConst>());
    xSemaphoreTake(stateMutex_, portMAX_DELAY);
    updateLightingSide(state_.ambient, config["ambient"].as<JsonObjectConst>());
    updateLightingSide(state_.keys, config["keys"].as<JsonObjectConst>());
    const float previousBrightness = state_.lightingBrightness;
    state_.lightingBrightness = synchronizedLightingBrightness(
        state_.lightingBrightness, observed, brightnessObservable);
    displayPower_.observeLightingConfig(allOff);
    state_.displayAwake = displayPower_.awake();
    state_.dirty = true;
    const float synchronizedBrightness = state_.lightingBrightness;
    xSemaphoreGive(stateMutex_);
    if (previousBrightness != synchronizedBrightness) {
      Serial.printf("Lighting brightness=%.0f%%\n", synchronizedBrightness * 100.0F);
    }
    sendSuccess(id);
    return;
  }
  if (std::strcmp(method, "lights.preview") == 0 ||
      std::strcmp(method, "host.focused_app") == 0) {
    sendSuccess(id);
    return;
  }

  StaticJsonDocument<192> response;
  response["id"] = id;
  JsonObject error = response.createNestedObject("error");
  error["code"] = -32601;
  error["message"] = "Method not found";
  String json;
  serializeJson(response, json);
  sendJson(json);
}

void CodexMicroBle::sendResult(JsonVariantConst id, JsonVariantConst result) {
  DynamicJsonDocument response(512);
  response["id"] = id;
  response["result"] = result;
  String json;
  serializeJson(response, json);
  sendJson(json);
}

void CodexMicroBle::sendSuccess(JsonVariantConst id) {
  StaticJsonDocument<96> response;
  response["id"] = id;
  response["result"] = true;
  String json;
  serializeJson(response, json);
  sendJson(json);
}

void CodexMicroBle::sendJson(const String& json) {
  if (txQueue_ == nullptr || stateMutex_ == nullptr) return;
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  const bool connected = state_.connected;
  xSemaphoreGive(stateMutex_);
  if (!connected) return;

  if (json.length() + 1 >= sizeof(QueuedMessage::payload)) {
    Serial.println("RPC output exceeded limit");
    return;
  }
  QueuedMessage message;
  message.length = static_cast<std::uint16_t>(json.length() + 1);
  std::memcpy(message.payload, json.c_str(), json.length());
  message.payload[json.length()] = '\n';
  if (xQueueSend(txQueue_, &message, 0) != pdTRUE) Serial.println("RPC output queue full");
}

void CodexMicroBle::flushJson(const QueuedMessage& message) {
  if (input_ == nullptr || message.length == 0) return;
  std::size_t offset = 0;
  while (offset < message.length) {
    const std::size_t chunk = std::min<std::size_t>(kPayloadSize, message.length - offset);
    std::uint8_t report[kReportBodySize] = {};
    report[0] = 2;
    report[1] = static_cast<std::uint8_t>(chunk);
    std::memcpy(report + 2, message.payload + offset, chunk);
    input_->setValue(report, sizeof(report));
    input_->notify();
    offset += chunk;
    delay(4);
  }
}

void CodexMicroBle::updateThreadLighting(JsonArrayConst values) {
  std::uint8_t updatedMask = 0;
  bool allOff = true;
  for (JsonObjectConst value : values) {
    const int id = value["id"] | -1;
    if (id < 0 || id >= static_cast<int>(state_.threads.size())) continue;
    updatedMask = static_cast<std::uint8_t>(updatedMask | (1U << id));
    allOff = allOff && isLightingValueOff(value);
  }
  const float observed = observedBrightness(values);
  const bool brightnessObservable = hasObservableBrightness(values);

  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  const float previousBrightness = state_.lightingBrightness;
  state_.lightingBrightness = synchronizedLightingBrightness(
      state_.lightingBrightness, observed, brightnessObservable);
  displayPower_.observeThreadLighting(updatedMask, allOff, millis());
  state_.displayAwake = displayPower_.awake();
  const bool inactivityOff = updatedMask == kAllThreadsMask && allOff &&
                             !state_.displayAwake;
  if (!inactivityOff) {
    for (JsonObjectConst value : values) {
      const int id = value["id"] | -1;
      if (id < 0 || id >= static_cast<int>(state_.threads.size())) continue;
      ThreadLight& light = state_.threads[id];
      light.color = value["c"] | light.color;
      light.brightness = value["b"] | light.brightness;
      light.effect = value["e"] | light.effect;
      light.speed = value["s"] | light.speed;
    }
  }
  state_.dirty = true;
  const float synchronizedBrightness = state_.lightingBrightness;
  xSemaphoreGive(stateMutex_);
  if (previousBrightness != synchronizedBrightness) {
    Serial.printf("Lighting brightness=%.0f%%\n", synchronizedBrightness * 100.0F);
  }
}

void CodexMicroBle::updateLightingSide(LightingSide& side, JsonObjectConst value) {
  if (value.isNull()) return;
  side.color = value["c"] | side.color;
  side.brightness = value["b"] | side.brightness;
  side.effect = value["e"] | side.effect;
  side.speed = value["s"] | side.speed;
}
