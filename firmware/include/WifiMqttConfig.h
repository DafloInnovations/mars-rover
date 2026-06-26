#ifndef WIFI_MQTT_CONFIG_H
#define WIFI_MQTT_CONFIG_H

#include <stdint.h>

// Replace these placeholders locally before uploading to hardware.
// Do not commit real Wi-Fi or MQTT credentials.
constexpr const char* WIFI_SSID = "BELL622";
constexpr const char* WIFI_PASSWORD = "1DC17A45F366";

constexpr const char* MQTT_BROKER_HOST = "82f65a1c6e3244e7ae415a9ca8725bf3.s1.eu.hivemq.cloud";
constexpr uint16_t MQTT_BROKER_PORT = 8883;
constexpr bool MQTT_TLS_ENABLED = true;
constexpr const char* MQTT_USERNAME = "NithinGangadhar";
constexpr const char* MQTT_PASSWORD = "Sumasu@91288";

constexpr const char* ROVER_ID = "supar1";

#endif  // WIFI_MQTT_CONFIG_H
