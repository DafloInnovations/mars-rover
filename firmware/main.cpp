#include <Arduino.h>
#include <ctype.h>
#include <string.h>

#include "include/WifiMqttConfig.h"
#include "motors/MotorController.h"
#include "mqtt/MqttClientManager.h"
#include "rfid/RFIDReader.h"
#include "sensors/LineSensor.h"
#include "servo/CargoServo.h"

namespace {

// L298N direction inputs. Hardware-specific GPIO assignments remain outside
// MotorController and are injected through its constructor.
constexpr uint8_t kIn1Pin = 25;
constexpr uint8_t kIn2Pin = 26;
constexpr uint8_t kIn3Pin = 27;
constexpr uint8_t kIn4Pin = 14;

// Three-channel line sensor digital inputs.
constexpr uint8_t kLineSensorLeftPin = 32;
constexpr uint8_t kLineSensorCenterPin = 33;
constexpr uint8_t kLineSensorRightPin = 21;
constexpr uint8_t kLineSensorBlackState = HIGH;

// RC522 SPI wiring. The reader must be powered from 3.3 V only.
constexpr uint8_t kRfidSsPin = 5;
constexpr uint8_t kRfidClockPin = 18;
constexpr uint8_t kRfidMosiPin = 23;
constexpr uint8_t kRfidMisoPin = 19;
constexpr uint8_t kRfidResetPin = 22;

// SG90 cargo mechanism control signal.
constexpr uint8_t kCargoServoPin = 13;
constexpr uint8_t kCargoServoClosedAngle = 0;
constexpr uint8_t kCargoServoOpenAngle = 90;

constexpr unsigned long kSerialBaudRate = 115200;
constexpr unsigned long kMovementDurationMs = 2000;
constexpr unsigned long kMissionStepDurationMs = 1000;
constexpr unsigned long kStandardStopDurationMs = 1000;
constexpr unsigned long kFinalStopDurationMs = 2000;
constexpr unsigned long kLineTestDurationMs = 20000;
constexpr unsigned long kLineSampleIntervalMs = 300;
constexpr unsigned long kRfidTestDurationMs = 20000;
constexpr unsigned long kSelfTestRfidDurationMs = 5000;
constexpr unsigned long kRfidScanIntervalMs = 50;
constexpr unsigned long kSelfTestMovementDurationMs = 1000;
constexpr unsigned long kStatusPublishIntervalMs = 2000;
constexpr size_t kCommandBufferSize = 32;
constexpr size_t kMqttCommandBufferSize = 64;

MotorController motorController(kIn1Pin, kIn2Pin, kIn3Pin, kIn4Pin);
LineSensor lineSensor(kLineSensorLeftPin,
                      kLineSensorCenterPin,
                      kLineSensorRightPin,
                      kLineSensorBlackState);
RFIDReader rfidReader(kRfidSsPin,
                      kRfidResetPin,
                      kRfidClockPin,
                      kRfidMosiPin,
                      kRfidMisoPin);
CargoServo cargoServo(kCargoServoPin,
                      kCargoServoClosedAngle,
                      kCargoServoOpenAngle);
MqttClientManager mqttManager(ROVER_ID,
                              WIFI_SSID,
                              WIFI_PASSWORD,
                              MQTT_BROKER_HOST,
                              MQTT_BROKER_PORT,
                              MQTT_TLS_ENABLED,
                              MQTT_USERNAME,
                              MQTT_PASSWORD);
char commandBuffer[kCommandBufferSize] = {};
size_t commandLength = 0;
bool commandOverflowed = false;
char mqttCommandBuffer[kMqttCommandBufferSize] = {};
bool mqttCommandPending = false;
unsigned long lastStatusPublishMs = 0;
bool wasWifiConnected = false;
bool wasMqttConnected = false;
char currentMission[16] = "none";
char roverState[18] = "IDLE";
char roverLocation[12] = "base";
char lastRfidUid[24] = "NONE";
char servoState[8] = "CLOSED";

void publishRoverStatus();

/**
 * @brief Updates the rover's high-level state for MQTT status reporting.
 *
 * @param state One of IDLE, MOVING, MISSION_RUNNING, or ERROR.
 */
void setRoverState(const char* state) {
  strncpy(roverState, state, sizeof(roverState) - 1);
  roverState[sizeof(roverState) - 1] = '\0';
}

/**
 * @brief Updates the current mission field for MQTT status reporting.
 *
 * @param mission Mission command or none.
 */
void setCurrentMission(const char* mission) {
  strncpy(currentMission, mission, sizeof(currentMission) - 1);
  currentMission[sizeof(currentMission) - 1] = '\0';
}

/**
 * @brief Updates the current location label for MQTT status reporting.
 *
 * @param location Simulated location label.
 */
void setRoverLocation(const char* location) {
  strncpy(roverLocation, location, sizeof(roverLocation) - 1);
  roverLocation[sizeof(roverLocation) - 1] = '\0';
}

/**
 * @brief Updates the last detected RFID UID for MQTT status reporting.
 */
void captureLastRfidUid() {
  const String uid = rfidReader.readUID();
  if (uid.length() == 0) {
    return;
  }

  uid.toCharArray(lastRfidUid, sizeof(lastRfidUid));
}

/**
 * @brief Publishes a firmware log message when MQTT is connected.
 *
 * @param message Null-terminated event text for mars/supar1/log.
 */
void publishLog(const char* message) {
  mqttManager.publishLog(message);
}

/**
 * @brief Publishes a firmware log message with one appended command value.
 *
 * @param prefix Event prefix such as ACK: or MISSION_START:.
 * @param command Command associated with the event.
 */
void publishCommandLog(const char* prefix, const char* command) {
  char logMessage[80] = {};
  snprintf(logMessage, sizeof(logMessage), "%s%s", prefix, command);
  publishLog(logMessage);
}

/**
 * @brief Publishes the MQTT birth message announcing rover capabilities.
 */
void publishBirthMessage() {
  constexpr const char* kBirthPayload =
      "{\"event\":\"ROVER_ONLINE\",\"rover_id\":\"supar1\",\"firmware\":\"v0.9\","
      "\"capabilities\":[\"motors\",\"rfid\",\"line_sensor\",\"servo\",\"self_test\",\"mqtt\"]}";

  publishLog(kBirthPayload);
}

/**
 * @brief Normalizes command text to the uppercase firmware protocol format.
 *
 * @param source Raw source text.
 * @param destination Output command buffer.
 * @param destinationSize Size of the output buffer.
 * @return true when a non-empty command was produced.
 */
bool normalizeCommand(const char* source, char* destination, const size_t destinationSize) {
  if (source == nullptr || destination == nullptr || destinationSize == 0) {
    return false;
  }

  while (isspace(static_cast<unsigned char>(*source))) {
    ++source;
  }

  size_t length = 0;
  while (*source != '\0' && length < destinationSize - 1) {
    destination[length++] =
        static_cast<char>(toupper(static_cast<unsigned char>(*source)));
    ++source;
  }

  while (length > 0 && isspace(static_cast<unsigned char>(destination[length - 1]))) {
    --length;
  }

  destination[length] = '\0';
  return length > 0;
}

/**
 * @brief Returns whether a command is handled by the shared command executor.
 *
 * @param command Normalized command.
 * @return true if the command is supported by v0.9 firmware.
 */
bool isSupportedCommand(const char* command) {
  return strcmp(command, "FORWARD") == 0 ||
         strcmp(command, "BACKWARD") == 0 ||
         strcmp(command, "LEFT") == 0 ||
         strcmp(command, "RIGHT") == 0 ||
         strcmp(command, "STOP") == 0 ||
         strcmp(command, "TEST") == 0 ||
         strcmp(command, "LINE_TEST") == 0 ||
         strcmp(command, "RFID_TEST") == 0 ||
         strcmp(command, "SERVO_OPEN") == 0 ||
         strcmp(command, "SERVO_CLOSE") == 0 ||
         strcmp(command, "SERVO_TEST") == 0 ||
         strcmp(command, "SELF_TEST") == 0 ||
         strcmp(command, "MISSION_1") == 0 ||
         strcmp(command, "MISSION_2") == 0 ||
         strcmp(command, "MISSION_3") == 0 ||
         strcmp(command, "MISSION_4") == 0 ||
         strcmp(command, "MISSION_5") == 0;
}

/**
 * @brief Stops the rover and keeps it stationary for the requested interval.
 *
 * @param durationMs Time to remain stopped, in milliseconds.
 */
void stopFor(const unsigned long durationMs) {
  Serial.println("TEST:STOP");
  motorController.stop();
  setRoverState("IDLE");
  delay(durationMs);
}

/**
 * @brief Runs the legacy v0.2 motor test sequence exactly once.
 *
 * This blocking diagnostic is only executed when the operator sends TEST. The
 * rover remains stopped after the sequence completes.
 */
void runMotorTest() {
  Serial.println("TEST:FORWARD");
  setRoverState("MOVING");
  motorController.forward();
  delay(kMovementDurationMs);
  stopFor(kStandardStopDurationMs);

  Serial.println("TEST:BACKWARD");
  setRoverState("MOVING");
  motorController.backward();
  delay(kMovementDurationMs);
  stopFor(kStandardStopDurationMs);

  Serial.println("TEST:LEFT");
  setRoverState("MOVING");
  motorController.left();
  delay(kMovementDurationMs);
  stopFor(kStandardStopDurationMs);

  Serial.println("TEST:RIGHT");
  setRoverState("MOVING");
  motorController.right();
  delay(kMovementDurationMs);
  stopFor(kFinalStopDurationMs);
}

/**
 * @brief Runs one predefined v0.4 mission movement sequence.
 *
 * Missions are intentionally deterministic placeholders. They provide an
 * end-to-end command path from Mission Control to the rover while autonomous
 * navigation and sensors remain outside this firmware release.
 *
 * @param command Mission command from MISSION_1 through MISSION_5.
 */
void runMission(const char* command) {
  setCurrentMission(command);
  setRoverState("MISSION_RUNNING");
  publishRoverStatus();

  Serial.print("MISSION_START:");
  Serial.println(command);
  publishCommandLog("MISSION_START:", command);

  setRoverState("MOVING");
  publishRoverStatus();
  if (strcmp(command, "MISSION_1") == 0) {
    motorController.forward();
    delay(kMovementDurationMs);
  } else if (strcmp(command, "MISSION_2") == 0) {
    motorController.forward();
    delay(kMovementDurationMs);
    motorController.left();
    delay(kMissionStepDurationMs);
  } else if (strcmp(command, "MISSION_3") == 0) {
    motorController.forward();
    delay(kMovementDurationMs);
    motorController.right();
    delay(kMissionStepDurationMs);
  } else if (strcmp(command, "MISSION_4") == 0) {
    motorController.forward();
    delay(kMissionStepDurationMs);
    motorController.left();
    delay(kMissionStepDurationMs);
    motorController.forward();
    delay(kMissionStepDurationMs);
  } else {
    // executeCommand() only calls this function for MISSION_5 at present.
    motorController.backward();
    delay(kMovementDurationMs);
  }

  motorController.stop();
  setRoverState("IDLE");
  Serial.print("MISSION_COMPLETE:");
  Serial.println(command);
  publishCommandLog("MISSION_COMPLETE:", command);
  setCurrentMission("none");
  setRoverLocation("base");
  publishRoverStatus();
}

/**
 * @brief Samples the BFD-1000 every 300 ms for a total of 20 seconds.
 *
 * Motors are stopped before diagnostics begin so sensor testing cannot leave
 * the rover moving. Sampling is scheduled from the original start time so
 * serial printing does not accumulate drift between samples.
 */
void runLineTest() {
  motorController.stop();
  const unsigned long startTime = millis();
  unsigned long nextSampleOffsetMs = 0;

  while (millis() - startTime < kLineTestDurationMs) {
    lineSensor.printReadings();
    nextSampleOffsetMs += kLineSampleIntervalMs;

    while (millis() - startTime < kLineTestDurationMs &&
           millis() - startTime < nextSampleOffsetMs) {
      const unsigned long elapsed = millis() - startTime;
      const unsigned long untilNextSample = nextSampleOffsetMs - elapsed;
      const unsigned long untilComplete = kLineTestDurationMs - elapsed;
      delay(min(untilNextSample, untilComplete));
    }
  }

  Serial.println("LINE_TEST_COMPLETE");
}

/**
 * @brief Scans for RC522-compatible cards for a total of 20 seconds.
 *
 * The rover is stopped before scanning. Each newly presented card is printed
 * once using its hexadecimal UID; no RFID value affects mission behavior.
 */
void runRfidTest() {
  motorController.stop();
  const unsigned long startTime = millis();

  while (millis() - startTime < kRfidTestDurationMs) {
    if (rfidReader.isCardPresent()) {
      captureLastRfidUid();
      rfidReader.printUID();
    }
    delay(kRfidScanIntervalMs);
  }

  Serial.println("RFID_TEST_COMPLETE");
}

/**
 * @brief Runs a complete one-shot hardware diagnostic across every module.
 *
 * Missing modules produce warnings and are skipped so the remaining hardware
 * can still be exercised. Motor commands always finish in a stopped state.
 */
void runSelfTest() {
  Serial.println("SELF_TEST_START");

  if (!motorController.isInitialized()) {
    Serial.println("WARN:MOTOR_NOT_INITIALIZED");
  } else {
    motorController.forward();
    delay(kSelfTestMovementDurationMs);
    motorController.stop();

    motorController.backward();
    delay(kSelfTestMovementDurationMs);
    motorController.stop();

    motorController.left();
    delay(kSelfTestMovementDurationMs);
    motorController.stop();

    motorController.right();
    delay(kSelfTestMovementDurationMs);
    motorController.stop();
  }

  if (!lineSensor.isInitialized()) {
    Serial.println("WARN:LINE_SENSOR_NOT_INITIALIZED");
  } else {
    lineSensor.printReadings();
  }

  if (!rfidReader.isInitialized()) {
    Serial.println("WARN:RFID_NOT_INITIALIZED");
  } else {
    const unsigned long rfidStartTime = millis();
    while (millis() - rfidStartTime < kSelfTestRfidDurationMs) {
      if (rfidReader.isCardPresent()) {
        captureLastRfidUid();
        rfidReader.printUID();
      }
      delay(kRfidScanIntervalMs);
    }
  }

  if (!cargoServo.isInitialized()) {
    Serial.println("WARN:SERVO_NOT_INITIALIZED");
  } else {
    cargoServo.test();
    strncpy(servoState, "CLOSED", sizeof(servoState) - 1);
    servoState[sizeof(servoState) - 1] = '\0';
  }

  Serial.println("SELF_TEST_COMPLETE");
}

/**
 * @brief Executes one normalized serial command and emits its protocol reply.
 *
 * @param command Null-terminated, uppercase command received from Serial.
 */
void executeCommand(const char* command) {
  if (strcmp(command, "FORWARD") == 0) {
    setRoverState("MOVING");
    motorController.forward();
  } else if (strcmp(command, "BACKWARD") == 0) {
    setRoverState("MOVING");
    motorController.backward();
  } else if (strcmp(command, "LEFT") == 0) {
    setRoverState("MOVING");
    motorController.left();
  } else if (strcmp(command, "RIGHT") == 0) {
    setRoverState("MOVING");
    motorController.right();
  } else if (strcmp(command, "STOP") == 0) {
    motorController.stop();
    setRoverState("IDLE");
  } else if (strcmp(command, "TEST") == 0) {
    runMotorTest();
  } else if (strcmp(command, "LINE_TEST") == 0) {
    Serial.println("ACK:LINE_TEST");
    runLineTest();
    return;
  } else if (strcmp(command, "RFID_TEST") == 0) {
    Serial.println("ACK:RFID_TEST");
    runRfidTest();
    return;
  } else if (strcmp(command, "SERVO_OPEN") == 0) {
    cargoServo.open();
    strncpy(servoState, "OPEN", sizeof(servoState) - 1);
    servoState[sizeof(servoState) - 1] = '\0';
  } else if (strcmp(command, "SERVO_CLOSE") == 0) {
    cargoServo.close();
    strncpy(servoState, "CLOSED", sizeof(servoState) - 1);
    servoState[sizeof(servoState) - 1] = '\0';
  } else if (strcmp(command, "SERVO_TEST") == 0) {
    cargoServo.test();
    strncpy(servoState, "CLOSED", sizeof(servoState) - 1);
    servoState[sizeof(servoState) - 1] = '\0';
  } else if (strcmp(command, "SELF_TEST") == 0) {
    runSelfTest();
  } else if (strcmp(command, "MISSION_1") == 0 ||
             strcmp(command, "MISSION_2") == 0 ||
             strcmp(command, "MISSION_3") == 0 ||
             strcmp(command, "MISSION_4") == 0 ||
             strcmp(command, "MISSION_5") == 0) {
    Serial.print("ACK:");
    Serial.println(command);
    runMission(command);
    return;
  } else {
    Serial.println("ERR:UNKNOWN_COMMAND");
    setRoverState("ERROR");
    publishLog("ERR:UNKNOWN_COMMAND");
    return;
  }

  Serial.print("ACK:");
  Serial.println(command);
}

/**
 * @brief Extracts a command from an MQTT payload.
 *
 * Supported formats are a plain command string, such as MISSION_3, or a small
 * JSON object containing a command property: {"command":"MISSION_3"}.
 *
 * @param payload MQTT payload text.
 * @param destination Output buffer for a normalized command.
 * @param destinationSize Output buffer size.
 * @return true when a command was extracted.
 */
bool extractMqttCommand(const char* payload, char* destination, const size_t destinationSize) {
  if (payload == nullptr || destination == nullptr || destinationSize == 0) {
    return false;
  }

  const char* commandStart = strstr(payload, "\"command\"");
  if (commandStart == nullptr) {
    commandStart = strstr(payload, "command");
  }

  if (commandStart != nullptr) {
    const char* valueStart = strchr(commandStart, ':');
    if (valueStart != nullptr) {
      ++valueStart;
      while (*valueStart != '\0' &&
             (isspace(static_cast<unsigned char>(*valueStart)) || *valueStart == '"')) {
        ++valueStart;
      }

      char rawCommand[kMqttCommandBufferSize] = {};
      size_t rawLength = 0;
      while (*valueStart != '\0' &&
             *valueStart != '"' &&
             *valueStart != ',' &&
             *valueStart != '}' &&
             !isspace(static_cast<unsigned char>(*valueStart)) &&
             rawLength < sizeof(rawCommand) - 1) {
        rawCommand[rawLength++] = *valueStart;
        ++valueStart;
      }
      rawCommand[rawLength] = '\0';
      return normalizeCommand(rawCommand, destination, destinationSize);
    }
  }

  return normalizeCommand(payload, destination, destinationSize);
}

/**
 * @brief Queues commands received from subscribed MQTT topics.
 *
 * The PubSubClient callback stays short and non-blocking. The main loop later
 * executes the queued command through the same handler used by Serial.
 *
 * @param topic MQTT topic that delivered the payload.
 * @param payload MQTT payload text.
 */
void handleMqttMessage(const char* topic, const char* payload) {
  if (strcmp(topic, mqttManager.missionTopic()) != 0 &&
      strcmp(topic, mqttManager.commandTopic()) != 0) {
    return;
  }

  char extractedCommand[kMqttCommandBufferSize] = {};
  if (!extractMqttCommand(payload, extractedCommand, sizeof(extractedCommand))) {
    publishLog("ERR:MQTT_EMPTY_COMMAND");
    return;
  }

  strncpy(mqttCommandBuffer, extractedCommand, sizeof(mqttCommandBuffer) - 1);
  mqttCommandBuffer[sizeof(mqttCommandBuffer) - 1] = '\0';
  mqttCommandPending = true;
}

/**
 * @brief Executes a command previously queued by the MQTT callback.
 */
void processMqttCommand() {
  if (!mqttCommandPending) {
    return;
  }

  char command[kMqttCommandBufferSize] = {};
  strncpy(command, mqttCommandBuffer, sizeof(command) - 1);
  mqttCommandPending = false;

  if (!isSupportedCommand(command)) {
    Serial.println("ERR:UNKNOWN_COMMAND");
    publishLog("ERR:UNKNOWN_COMMAND");
    return;
  }

  publishCommandLog("ACK:", command);
  executeCommand(command);
}

/**
 * @brief Publishes the current rover status JSON every two seconds.
 */
void publishRoverStatus() {
  const char* lineState = "UNKNOWN";
  if (lineSensor.isInitialized()) {
    lineState = lineSensor.isJunction() ? "AT_JUNCTION" : "ON_TRACK";
  }

  const long wifiRssi = mqttManager.isWifiConnected() ? WiFi.RSSI() : 0;
  const unsigned long uptimeSeconds = millis() / 1000;

  char statusPayload[384] = {};
  snprintf(statusPayload,
           sizeof(statusPayload),
           "{\"rover_id\":\"%s\",\"firmware\":\"v0.9\",\"mission\":\"%s\","
           "\"state\":\"%s\",\"location\":\"%s\",\"battery\":87,"
           "\"wifi_rssi\":%ld,\"uptime\":%lu,\"line\":\"%s\","
           "\"rfid\":\"%s\",\"servo\":\"%s\",\"timestamp\":%lu}",
           ROVER_ID,
           currentMission,
           roverState,
           roverLocation,
           wifiRssi,
           uptimeSeconds,
           lineState,
           lastRfidUid,
           servoState,
           uptimeSeconds);
  mqttManager.publishStatus(statusPayload);
}

/**
 * @brief Publishes the current rover status JSON every two seconds.
 */
void publishPeriodicStatus() {
  const unsigned long now = millis();
  if (now - lastStatusPublishMs < kStatusPublishIntervalMs) {
    return;
  }

  lastStatusPublishMs = now;
  publishRoverStatus();
}

/**
 * @brief Keeps Wi-Fi and MQTT connections alive while preserving rover safety.
 */
void processNetwork() {
  mqttManager.loop();

  const bool wifiConnected = mqttManager.isWifiConnected();
  const bool mqttConnected = mqttManager.isMqttConnected();

  if (wasWifiConnected && !wifiConnected) {
    motorController.stop();
    setRoverState("ERROR");
    Serial.println("ERR:WIFI_DISCONNECTED");
  }

  if (wasMqttConnected && !mqttConnected) {
    motorController.stop();
    setRoverState("ERROR");
    Serial.println("ERR:MQTT_DISCONNECTED");
  }

  if (!wasWifiConnected && wifiConnected) {
    Serial.println("WIFI_CONNECTED");
    publishLog("WIFI_CONNECTED");
  }

  if (!wasMqttConnected && mqttConnected) {
    if (strcmp(roverState, "ERROR") == 0) {
      setRoverState("IDLE");
    }
    publishBirthMessage();
  }

  wasWifiConnected = wifiConnected;
  wasMqttConnected = mqttConnected;

  publishPeriodicStatus();
}

/**
 * @brief Consumes available serial bytes without blocking the control loop.
 *
 * Commands are delimited by a newline. Carriage returns are ignored so both
 * LF and CRLF serial-monitor settings are accepted. Input is normalized to
 * uppercase, allowing commands to be entered without case sensitivity.
 */
void processSerialInput() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      if (commandOverflowed) {
        Serial.println("ERR:UNKNOWN_COMMAND");
      } else if (commandLength > 0) {
        commandBuffer[commandLength] = '\0';
        executeCommand(commandBuffer);
      }

      commandLength = 0;
      commandOverflowed = false;
      continue;
    }

    if (commandOverflowed) {
      continue;
    }

    if (commandLength < kCommandBufferSize - 1) {
      commandBuffer[commandLength++] =
          static_cast<char>(toupper(static_cast<unsigned char>(incoming)));
    } else {
      commandOverflowed = true;
    }
  }
}

}  // namespace

/**
 * @brief Initializes serial diagnostics and the L298N motor controller.
 *
 * Arduino invokes setup() once after the ESP32 boots or resets. Calling
 * MotorController::begin() configures all direction pins and establishes a
 * safe stopped state before serial commands are accepted.
 */
void setup() {
  Serial.begin(kSerialBaudRate);
  motorController.begin();
  lineSensor.begin();
  rfidReader.begin();
  cargoServo.begin();
  mqttManager.begin(handleMqttMessage);

  Serial.println();
  Serial.println("Mission to Mars 2050 - Su-Par1");
  Serial.println("Firmware v0.9 - MQTT Mission Mode");
  Serial.println("Awaiting commands...");
}

/**
 * @brief Services operator commands while leaving movement under manual control.
 *
 * No movement starts automatically. Manual motor commands, the legacy TEST
 * sequence, five simulated missions, line diagnostics, RFID diagnostics, and
 * cargo-servo diagnostics, and the combined hardware self-test are available
 * on demand.
 */
void loop() {
  processNetwork();
  processSerialInput();
  processMqttCommand();
}
