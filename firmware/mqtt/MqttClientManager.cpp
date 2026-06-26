#include "MqttClientManager.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr unsigned long kWifiReconnectIntervalMs = 10000;
constexpr unsigned long kMqttReconnectIntervalMs = 5000;
constexpr unsigned long kInitialConnectAttemptMs = 0;

bool hasText(const char* value) {
  return value != nullptr && value[0] != '\0';
}

}  // namespace

MqttClientManager* MqttClientManager::activeInstance_ = nullptr;

MqttClientManager::MqttClientManager(const char* roverId,
                                     const char* wifiSsid,
                                     const char* wifiPassword,
                                     const char* brokerHost,
                                     uint16_t brokerPort,
                                     bool mqttTlsEnabled,
                                     const char* mqttUsername,
                                     const char* mqttPassword)
    : roverId_(roverId),
      wifiSsid_(wifiSsid),
      wifiPassword_(wifiPassword),
      brokerHost_(brokerHost),
      brokerPort_(brokerPort),
      mqttTlsEnabled_(mqttTlsEnabled || brokerPort == 8883),
      mqttUsername_(mqttUsername),
      mqttPassword_(mqttPassword),
      mqttClient_(wifiClient_),
      callback_(nullptr),
      missionTopic_{},
      commandTopic_{},
      statusTopic_{},
      telemetryTopic_{},
      logTopic_{},
      clientId_{},
      lastWifiAttemptMs_(kInitialConnectAttemptMs),
      lastMqttAttemptMs_(kInitialConnectAttemptMs) {
  buildTopics();
}

void MqttClientManager::begin(MqttMessageCallback callback) {
  callback_ = callback;
  activeInstance_ = this;

  WiFi.mode(WIFI_STA);
  if (mqttTlsEnabled_) {
    secureClient_.setInsecure();
    mqttClient_.setClient(secureClient_);
    Serial.println("MQTT_TLS_ENABLED");
  } else {
    mqttClient_.setClient(wifiClient_);
  }
  mqttClient_.setServer(brokerHost_, brokerPort_);
  mqttClient_.setCallback(MqttClientManager::handleMqttMessage);

  connectWifiIfNeeded();
  connectMqttIfNeeded();
}

void MqttClientManager::loop() {
  connectWifiIfNeeded();
  connectMqttIfNeeded();

  if (mqttClient_.connected()) {
    mqttClient_.loop();
  }
}

void MqttClientManager::disconnect() {
  if (mqttClient_.connected()) {
    mqttClient_.disconnect();
  }
  WiFi.disconnect();
}

bool MqttClientManager::isWifiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool MqttClientManager::isMqttConnected() {
  return mqttClient_.connected();
}

bool MqttClientManager::publish(const char* topic, const char* payload) {
  if (!mqttClient_.connected()) {
    return false;
  }
  return mqttClient_.publish(topic, payload);
}

bool MqttClientManager::publishLog(const char* message) {
  return publish(logTopic_, message);
}

bool MqttClientManager::publishStatus(const char* payload) {
  return publish(statusTopic_, payload);
}

const char* MqttClientManager::missionTopic() const {
  return missionTopic_;
}

const char* MqttClientManager::commandTopic() const {
  return commandTopic_;
}

const char* MqttClientManager::statusTopic() const {
  return statusTopic_;
}

const char* MqttClientManager::telemetryTopic() const {
  return telemetryTopic_;
}

const char* MqttClientManager::logTopic() const {
  return logTopic_;
}

void MqttClientManager::handleMqttMessage(char* topic,
                                          uint8_t* payload,
                                          unsigned int length) {
  if (activeInstance_ != nullptr) {
    activeInstance_->onMqttMessage(topic, payload, length);
  }
}

void MqttClientManager::onMqttMessage(char* topic,
                                      uint8_t* payload,
                                      unsigned int length) {
  if (callback_ == nullptr) {
    return;
  }

  constexpr size_t kPayloadBufferSize = 160;
  char payloadBuffer[kPayloadBufferSize] = {};
  const size_t copyLength = min(static_cast<size_t>(length), kPayloadBufferSize - 1);

  memcpy(payloadBuffer, payload, copyLength);
  payloadBuffer[copyLength] = '\0';
  callback_(topic, payloadBuffer);
}

void MqttClientManager::connectWifiIfNeeded() {
  if (isWifiConnected() || !hasText(wifiSsid_) || strcmp(wifiSsid_, "YOUR_WIFI_SSID") == 0) {
    return;
  }

  const unsigned long now = millis();
  if (lastWifiAttemptMs_ != kInitialConnectAttemptMs &&
      now - lastWifiAttemptMs_ < kWifiReconnectIntervalMs) {
    return;
  }

  lastWifiAttemptMs_ = now;
  Serial.print("WIFI_CONNECTING:");
  Serial.println(wifiSsid_);
  WiFi.begin(wifiSsid_, wifiPassword_);
}

void MqttClientManager::connectMqttIfNeeded() {
  if (!isWifiConnected() || mqttClient_.connected() || !hasText(brokerHost_)) {
    return;
  }

  const unsigned long now = millis();
  if (lastMqttAttemptMs_ != kInitialConnectAttemptMs &&
      now - lastMqttAttemptMs_ < kMqttReconnectIntervalMs) {
    return;
  }

  lastMqttAttemptMs_ = now;
  snprintf(clientId_, sizeof(clientId_), "supar1-%08lX", static_cast<unsigned long>(ESP.getEfuseMac()));

  Serial.print("MQTT_CONNECTING:");
  Serial.print(brokerHost_);
  Serial.print(":");
  Serial.println(brokerPort_);

  bool connected = false;
  if (hasText(mqttUsername_)) {
    connected = mqttClient_.connect(clientId_, mqttUsername_, mqttPassword_);
  } else {
    connected = mqttClient_.connect(clientId_);
  }

  if (!connected) {
    Serial.print("ERR:MQTT_CONNECT_FAILED:");
    Serial.println(mqttClient_.state());
    return;
  }

  mqttClient_.subscribe(missionTopic_);
  mqttClient_.subscribe(commandTopic_);
  Serial.println("MQTT_CONNECTED");
  publishLog("MQTT_CONNECTED");
}

void MqttClientManager::buildTopics() {
  snprintf(missionTopic_, sizeof(missionTopic_), "mars/%s/mission", roverId_);
  snprintf(commandTopic_, sizeof(commandTopic_), "mars/%s/command", roverId_);
  snprintf(statusTopic_, sizeof(statusTopic_), "mars/%s/status", roverId_);
  snprintf(telemetryTopic_, sizeof(telemetryTopic_), "mars/%s/telemetry", roverId_);
  snprintf(logTopic_, sizeof(logTopic_), "mars/%s/log", roverId_);
}
