#ifndef MQTT_CLIENT_MANAGER_H
#define MQTT_CLIENT_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

using MqttMessageCallback = void (*)(const char* topic, const char* payload);

/**
 * @brief Owns Wi-Fi and MQTT connectivity for the Su-Par1 rover.
 *
 * The manager keeps network code out of the mission command handler. It
 * reconnects opportunistically, publishes logs/status messages, and forwards
 * subscribed MQTT payloads to a firmware-level callback.
 */
class MqttClientManager {
 public:
  MqttClientManager(const char* roverId,
                    const char* wifiSsid,
                    const char* wifiPassword,
                    const char* brokerHost,
                    uint16_t brokerPort,
                    bool mqttTlsEnabled,
                    const char* mqttUsername,
                    const char* mqttPassword);

  void begin(MqttMessageCallback callback);
  void loop();
  void disconnect();
  bool isWifiConnected() const;
  bool isMqttConnected();
  bool publish(const char* topic, const char* payload);
  bool publishLog(const char* message);
  bool publishStatus(const char* payload);

  const char* missionTopic() const;
  const char* commandTopic() const;
  const char* statusTopic() const;
  const char* telemetryTopic() const;
  const char* logTopic() const;

 private:
  static MqttClientManager* activeInstance_;

  static void handleMqttMessage(char* topic, uint8_t* payload, unsigned int length);
  void onMqttMessage(char* topic, uint8_t* payload, unsigned int length);
  void connectWifiIfNeeded();
  void connectMqttIfNeeded();
  void buildTopics();

  const char* roverId_;
  const char* wifiSsid_;
  const char* wifiPassword_;
  const char* brokerHost_;
  uint16_t brokerPort_;
  bool mqttTlsEnabled_;
  const char* mqttUsername_;
  const char* mqttPassword_;

  WiFiClient wifiClient_;
  WiFiClientSecure secureClient_;
  PubSubClient mqttClient_;
  MqttMessageCallback callback_;

  char missionTopic_[48];
  char commandTopic_[48];
  char statusTopic_[48];
  char telemetryTopic_[48];
  char logTopic_[48];
  char clientId_[48];

  unsigned long lastWifiAttemptMs_;
  unsigned long lastMqttAttemptMs_;
};

#endif  // MQTT_CLIENT_MANAGER_H
