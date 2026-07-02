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
constexpr uint8_t kLineSensorBlackState = LOW;

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
constexpr unsigned long kLineFollowIntervalMs = 40;
constexpr unsigned long kLineFollowJunctionPauseMs = 300;
constexpr unsigned long kRfidApproachScanDurationMs = 1500;
constexpr unsigned long kRfidDebounceMs = 2000;
constexpr unsigned long kPickupPauseDurationMs = 1000;
constexpr unsigned long kLineFollowTestDurationMs = 20000;
constexpr unsigned long kRfidTestDurationMs = 20000;
constexpr unsigned long kSelfTestRfidDurationMs = 5000;
constexpr unsigned long kRfidScanIntervalMs = 50;
constexpr unsigned long kSelfTestMovementDurationMs = 1000;
constexpr unsigned long kStatusPublishIntervalMs = 2000;
constexpr size_t kCommandBufferSize = 32;
constexpr size_t kMqttCommandBufferSize = 64;
constexpr uint8_t LINE_FOLLOW_SPEED = 130;
constexpr uint8_t LINE_CORRECTION_SPEED = 110;
constexpr uint8_t RFID_APPROACH_SPEED = 90;

// Straight-line RFID checkpoint navigation:
// BASE -> FOOD -> MEDICINE -> OXYGEN -> HABITAT.
constexpr const char* kCheckpointBase = "BASE";
constexpr const char* kCheckpointFood = "FOOD";
constexpr const char* kCheckpointMedicine = "MEDICINE";
constexpr const char* kCheckpointOxygen = "OXYGEN";
constexpr const char* kCheckpointHabitat = "HABITAT";
constexpr const char* kCheckpointUnknown = "UNKNOWN";
constexpr const char* kUidBase = "536AC0FF220001";
constexpr const char* kUidFood = "5373C0FF220001";
constexpr const char* kUidMedicine = "5339C0FF220001";
constexpr const char* kUidOxygen = "533AC0FF220001";
constexpr const char* kUidHabitat = "5338C0FF220001";
// Additional RFID stickers can be added here without changing mission logic.
constexpr const char* kUidBase2 = "";      // BASE_UID_2
constexpr const char* kUidBase3 = "";      // BASE_UID_3
constexpr const char* kUidFood2 = "";      // FOOD_UID_2
constexpr const char* kUidFood3 = "";      // FOOD_UID_3
constexpr const char* kUidMedicine2 = "";  // MEDICINE_UID_2
constexpr const char* kUidMedicine3 = "";  // MEDICINE_UID_3
constexpr const char* kUidOxygen2 = "";    // OXYGEN_UID_2
constexpr const char* kUidOxygen3 = "";    // OXYGEN_UID_3
constexpr const char* kUidHabitat2 = "";   // HABITAT_UID_2
constexpr const char* kUidHabitat3 = "";   // HABITAT_UID_3
constexpr const char* kBaseUids[] = {kUidBase, kUidBase2, kUidBase3};
constexpr const char* kFoodUids[] = {kUidFood, kUidFood2, kUidFood3};
constexpr const char* kMedicineUids[] = {kUidMedicine, kUidMedicine2, kUidMedicine3};
constexpr const char* kOxygenUids[] = {kUidOxygen, kUidOxygen2, kUidOxygen3};
constexpr const char* kHabitatUids[] = {kUidHabitat, kUidHabitat2, kUidHabitat3};

enum LastLineDirection {
  LINE_DIR_CENTER,
  LINE_DIR_LEFT,
  LINE_DIR_RIGHT,
};

enum LineFollowState {
  LINE_STATE_UNKNOWN,
  LINE_STATE_CENTER,
  LINE_STATE_LEFT,
  LINE_STATE_RIGHT,
  LINE_STATE_JUNCTION,
  LINE_STATE_LOST,
};

enum MissionPhase {
  MISSION_PHASE_IDLE,
  MISSION_PHASE_GOING_TO_PICKUP,
  MISSION_PHASE_PICKUP_PAUSE,
  MISSION_PHASE_GOING_TO_DELIVERY,
  MISSION_PHASE_DELIVERED,
  MISSION_PHASE_RETURNING_BASE,
};

enum NavigationDirection {
  NAVIGATION_FORWARD_ROUTE,
  NAVIGATION_REVERSE_ROUTE,
};

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
char roverLocation[12] = "BASE";
char targetCheckpoint[12] = "UNKNOWN";
char currentCargo[12] = "NONE";
char pickupCheckpoint[12] = "NONE";
char deliveryCheckpoint[12] = "NONE";
char currentCheckpoint[12] = "BASE";
char cargoStatus[24] = "NONE";
char lastRfidUid[24] = "NONE";
char servoState[8] = "CLOSED";
bool lineFollowEnabled = false;
bool lineFollowTestActive = false;
bool missionNavigationActive = false;
unsigned long lineFollowTestStartedAt = 0;
unsigned long lastLineFollowUpdateMs = 0;
unsigned long lastLineFollowRfidScanMs = 0;
unsigned long lineFollowPausedUntilMs = 0;
unsigned long rfidApproachUntilMs = 0;
unsigned long lastRfidAcceptedAtMs = 0;
LastLineDirection lastLineDirection = LINE_DIR_CENTER;
LineFollowState currentLineFollowState = LINE_STATE_UNKNOWN;
MissionPhase missionPhase = MISSION_PHASE_IDLE;
NavigationDirection navigationDirection = NAVIGATION_FORWARD_ROUTE;
char lastProcessedRfidUid[24] = "";
char lastProcessedCheckpoint[12] = "";

void publishRoverStatus();

/**
 * @brief Copies a C string into a fixed-size firmware status field.
 */
void copyStatusField(char* destination, const size_t destinationSize, const char* value) {
  if (destination == nullptr || destinationSize == 0) {
    return;
  }

  const char* safeValue = value != nullptr ? value : "";
  strncpy(destination, safeValue, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

/**
 * @brief Updates the rover's high-level state for MQTT status reporting.
 *
 * @param state One of IDLE, MOVING, MISSION_RUNNING, or ERROR.
 */
void setRoverState(const char* state) {
  copyStatusField(roverState, sizeof(roverState), state);
}

/**
 * @brief Updates the current mission field for MQTT status reporting.
 *
 * @param mission Mission command or none.
 */
void setCurrentMission(const char* mission) {
  copyStatusField(currentMission, sizeof(currentMission), mission);
}

/**
 * @brief Updates the current location label for MQTT status reporting.
 *
 * @param location Simulated location label.
 */
void setRoverLocation(const char* location) {
  copyStatusField(roverLocation, sizeof(roverLocation), location);
}

/**
 * @brief Updates the active RFID checkpoint target for mission navigation.
 *
 * @param checkpoint One of BASE, FOOD, MEDICINE, OXYGEN, HABITAT, or UNKNOWN.
 */
void setTargetCheckpoint(const char* checkpoint) {
  copyStatusField(targetCheckpoint, sizeof(targetCheckpoint), checkpoint);
}

/**
 * @brief Updates the currently detected checkpoint and public location field.
 */
void setCurrentCheckpoint(const char* checkpoint) {
  copyStatusField(currentCheckpoint, sizeof(currentCheckpoint), checkpoint);
  setRoverLocation(checkpoint);
}

/**
 * @brief Converts the mission phase enum to the MQTT status string.
 */
const char* missionPhaseName(const MissionPhase phase) {
  switch (phase) {
    case MISSION_PHASE_GOING_TO_PICKUP:
      return "GOING_TO_PICKUP";
    case MISSION_PHASE_PICKUP_PAUSE:
      return "PICKUP_PAUSE";
    case MISSION_PHASE_GOING_TO_DELIVERY:
      return "GOING_TO_DELIVERY";
    case MISSION_PHASE_DELIVERED:
      return "DELIVERED";
    case MISSION_PHASE_RETURNING_BASE:
      return "RETURNING_BASE";
    case MISSION_PHASE_IDLE:
    default:
      return "IDLE";
  }
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
 * @brief Normalizes a UID by removing spaces/colons and forcing uppercase.
 */
String normalizeUid(const String& rawUid) {
  String normalized;
  normalized.reserve(rawUid.length());

  for (size_t index = 0; index < rawUid.length(); ++index) {
    const char character = rawUid.charAt(index);
    if (character == ':' || isspace(static_cast<unsigned char>(character))) {
      continue;
    }
    normalized += character;
  }

  normalized.toUpperCase();
  return normalized;
}

/**
 * @brief Compares an observed RFID UID against a configured checkpoint UID.
 */
bool uidMatches(const String& observedUid, const char* configuredUid) {
  if (observedUid.length() == 0 || configuredUid == nullptr ||
      configuredUid[0] == '\0') {
    return false;
  }

  return normalizeUid(observedUid) == normalizeUid(String(configuredUid));
}

/**
 * @brief Compares an observed RFID UID against every UID assigned to a checkpoint.
 */
bool uidMatchesAny(const String& observedUid,
                   const char* const configuredUids[],
                   const size_t configuredUidCount) {
  for (size_t index = 0; index < configuredUidCount; ++index) {
    if (uidMatches(observedUid, configuredUids[index])) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Maps an RFID UID to a named straight-line checkpoint.
 *
 * Unknown or placeholder UID mappings intentionally return UNKNOWN so the rover
 * can keep driving without crashing while tags are being commissioned.
 */
const char* checkpointForUid(const String& uid) {
  if (uidMatchesAny(uid, kBaseUids, sizeof(kBaseUids) / sizeof(kBaseUids[0]))) {
    return kCheckpointBase;
  }
  if (uidMatchesAny(uid, kFoodUids, sizeof(kFoodUids) / sizeof(kFoodUids[0]))) {
    return kCheckpointFood;
  }
  if (uidMatchesAny(uid,
                    kMedicineUids,
                    sizeof(kMedicineUids) / sizeof(kMedicineUids[0]))) {
    return kCheckpointMedicine;
  }
  if (uidMatchesAny(uid, kOxygenUids, sizeof(kOxygenUids) / sizeof(kOxygenUids[0]))) {
    return kCheckpointOxygen;
  }
  if (uidMatchesAny(uid,
                    kHabitatUids,
                    sizeof(kHabitatUids) / sizeof(kHabitatUids[0]))) {
    return kCheckpointHabitat;
  }

  return kCheckpointUnknown;
}

/**
 * @brief Returns the physical order index for a straight-line checkpoint.
 */
int checkpointIndex(const char* checkpoint) {
  if (strcmp(checkpoint, kCheckpointBase) == 0) {
    return 0;
  }
  if (strcmp(checkpoint, kCheckpointFood) == 0) {
    return 1;
  }
  if (strcmp(checkpoint, kCheckpointMedicine) == 0) {
    return 2;
  }
  if (strcmp(checkpoint, kCheckpointOxygen) == 0) {
    return 3;
  }
  if (strcmp(checkpoint, kCheckpointHabitat) == 0) {
    return 4;
  }

  return -1;
}

/**
 * @brief Converts navigation direction to the MQTT status string.
 */
const char* navigationDirectionName(const NavigationDirection direction) {
  return direction == NAVIGATION_REVERSE_ROUTE ? "REVERSE_ROUTE" : "FORWARD_ROUTE";
}

/**
 * @brief Chooses forward or reverse travel based on current and target checkpoints.
 *
 * @return true when the target is the current checkpoint and no travel is needed.
 */
bool updateNavigationDirectionForTarget(const char* target) {
  const int currentIndex = checkpointIndex(currentCheckpoint);
  const int targetIndex = checkpointIndex(target);

  if (currentIndex < 0 || targetIndex < 0) {
    navigationDirection = NAVIGATION_FORWARD_ROUTE;
    return false;
  }

  if (targetIndex > currentIndex) {
    navigationDirection = NAVIGATION_FORWARD_ROUTE;
    return false;
  }

  if (targetIndex < currentIndex) {
    navigationDirection = NAVIGATION_REVERSE_ROUTE;
    return false;
  }

  return true;
}

/**
 * @brief Selects the pickup checkpoint for a mission command.
 */
const char* pickupCheckpointForMission(const char* command) {
  if (strcmp(command, "MISSION_1") == 0) {
    return kCheckpointFood;
  }
  if (strcmp(command, "MISSION_2") == 0) {
    return kCheckpointMedicine;
  }
  if (strcmp(command, "MISSION_3") == 0) {
    return kCheckpointOxygen;
  }

  return "NONE";
}

/**
 * @brief Selects the delivery/destination checkpoint for a mission command.
 */
const char* deliveryCheckpointForMission(const char* command) {
  if (strcmp(command, "MISSION_5") == 0) {
    return kCheckpointBase;
  }
  if (strcmp(command, "MISSION_1") == 0 ||
      strcmp(command, "MISSION_2") == 0 ||
      strcmp(command, "MISSION_3") == 0 ||
      strcmp(command, "MISSION_4") == 0) {
    return kCheckpointHabitat;
  }

  return kCheckpointUnknown;
}

/**
 * @brief Selects the cargo label for a mission command.
 */
const char* cargoForMission(const char* command) {
  if (strcmp(command, "MISSION_1") == 0) {
    return kCheckpointFood;
  }
  if (strcmp(command, "MISSION_2") == 0) {
    return kCheckpointMedicine;
  }
  if (strcmp(command, "MISSION_3") == 0) {
    return kCheckpointOxygen;
  }

  return "NONE";
}

/**
 * @brief Prints a UID and its mapped checkpoint for diagnostics.
 */
void printRfidUidAndCheckpoint(const String& uid, const char* checkpoint) {
  Serial.print("RFID:UID=");
  Serial.print(normalizeUid(uid));
  Serial.print(",CHECKPOINT=");
  Serial.println(checkpoint);
}

/**
 * @brief Returns true when an RFID read is a recent duplicate that should stay quiet.
 */
bool isDuplicateRfidRead(const String& normalizedUid,
                         const char* checkpoint,
                         const unsigned long now) {
  if (now - lastRfidAcceptedAtMs >= kRfidDebounceMs) {
    return false;
  }

  if (normalizedUid.length() > 0 && strcmp(normalizedUid.c_str(), lastProcessedRfidUid) == 0) {
    return true;
  }

  return strcmp(checkpoint, kCheckpointUnknown) != 0 &&
         strcmp(checkpoint, lastProcessedCheckpoint) == 0;
}

/**
 * @brief Stores the last accepted RFID read for duplicate suppression.
 */
void rememberRfidRead(const String& normalizedUid,
                      const char* checkpoint,
                      const unsigned long now) {
  normalizedUid.toCharArray(lastProcessedRfidUid, sizeof(lastProcessedRfidUid));
  copyStatusField(lastProcessedCheckpoint, sizeof(lastProcessedCheckpoint), checkpoint);
  lastRfidAcceptedAtMs = now;
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
 * @brief Converts the line-follow state enum into the MQTT status value.
 */
const char* lineFollowStateName(const LineFollowState state) {
  switch (state) {
    case LINE_STATE_CENTER:
      return "CENTER";
    case LINE_STATE_LEFT:
      return "LEFT";
    case LINE_STATE_RIGHT:
      return "RIGHT";
    case LINE_STATE_JUNCTION:
      return "JUNCTION";
    case LINE_STATE_LOST:
      return "LOST";
    case LINE_STATE_UNKNOWN:
    default:
      return "CENTER";
  }
}

/**
 * @brief Converts the line-follow state into the legacy line status field.
 */
const char* lineStatusName(const LineFollowState state) {
  switch (state) {
    case LINE_STATE_JUNCTION:
      return "AT_JUNCTION";
    case LINE_STATE_LOST:
      return "LOST";
    case LINE_STATE_CENTER:
    case LINE_STATE_LEFT:
    case LINE_STATE_RIGHT:
      return "ON_TRACK";
    case LINE_STATE_UNKNOWN:
    default:
      return lineSensor.isInitialized() && lineSensor.isJunction()
                 ? "AT_JUNCTION"
                 : "UNKNOWN";
  }
}

/**
 * @brief Emits one line-follow transition log without spamming repeated states.
 */
void logLineFollowState(const LineFollowState state) {
  if (currentLineFollowState == state) {
    return;
  }

  currentLineFollowState = state;

  switch (state) {
    case LINE_STATE_CENTER:
      Serial.println("LINE:FOLLOWING_CENTER");
      publishLog("LINE:FOLLOWING_CENTER");
      break;
    case LINE_STATE_LEFT:
      Serial.println("LINE:CORRECT_LEFT");
      publishLog("LINE:CORRECT_LEFT");
      break;
    case LINE_STATE_RIGHT:
      Serial.println("LINE:CORRECT_RIGHT");
      publishLog("LINE:CORRECT_RIGHT");
      break;
    case LINE_STATE_JUNCTION:
      Serial.println("LINE:JUNCTION");
      publishLog("LINE:JUNCTION");
      publishRoverStatus();
      break;
    case LINE_STATE_LOST:
      Serial.println("LINE:LOST");
      publishLog("LINE:LOST");
      publishRoverStatus();
      break;
    case LINE_STATE_UNKNOWN:
    default:
      break;
  }
}

/**
 * @brief Stops and disables autonomous line following.
 */
void disableLineFollowing(const bool stopMotors) {
  lineFollowEnabled = false;
  lineFollowTestActive = false;
  missionNavigationActive = false;
  missionPhase = MISSION_PHASE_IDLE;
  lineFollowPausedUntilMs = 0;
  rfidApproachUntilMs = 0;
  setTargetCheckpoint(kCheckpointUnknown);
  copyStatusField(currentCargo, sizeof(currentCargo), "NONE");
  copyStatusField(pickupCheckpoint, sizeof(pickupCheckpoint), "NONE");
  copyStatusField(deliveryCheckpoint, sizeof(deliveryCheckpoint), "NONE");
  copyStatusField(cargoStatus, sizeof(cargoStatus), "NONE");
  navigationDirection = NAVIGATION_FORWARD_ROUTE;

  if (stopMotors) {
    motorController.stop();
    setRoverState("IDLE");
  }
}

/**
 * @brief Enables continuous autonomous line following.
 */
void startLineFollowing() {
  lineFollowEnabled = true;
  lineFollowTestActive = false;
  missionNavigationActive = false;
  lineFollowTestStartedAt = 0;
  lastLineFollowUpdateMs = 0;
  lastLineFollowRfidScanMs = 0;
  lineFollowPausedUntilMs = 0;
  rfidApproachUntilMs = 0;
  currentLineFollowState = LINE_STATE_UNKNOWN;
  lastLineDirection = LINE_DIR_CENTER;
  navigationDirection = NAVIGATION_FORWARD_ROUTE;
  setRoverState("MOVING");
  Serial.println("ACK:LINE_FOLLOW_START");
  publishLog("LINE_FOLLOW_STARTED");
}

/**
 * @brief Enables line following for a fixed test duration.
 */
void startLineFollowTest() {
  lineFollowEnabled = true;
  lineFollowTestActive = true;
  missionNavigationActive = false;
  lineFollowTestStartedAt = millis();
  lastLineFollowUpdateMs = 0;
  lastLineFollowRfidScanMs = 0;
  lineFollowPausedUntilMs = 0;
  rfidApproachUntilMs = 0;
  currentLineFollowState = LINE_STATE_UNKNOWN;
  lastLineDirection = LINE_DIR_CENTER;
  navigationDirection = NAVIGATION_FORWARD_ROUTE;
  setRoverState("MOVING");
  Serial.println("LINE_FOLLOW_TEST_START");
  publishLog("LINE_FOLLOW_TEST_START");
}

/**
 * @brief Resets mission-specific status fields when manual/diagnostic control takes over.
 */
void resetMissionNavigationState() {
  missionNavigationActive = false;
  missionPhase = MISSION_PHASE_IDLE;
  rfidApproachUntilMs = 0;
  setTargetCheckpoint(kCheckpointUnknown);
  copyStatusField(currentCargo, sizeof(currentCargo), "NONE");
  copyStatusField(pickupCheckpoint, sizeof(pickupCheckpoint), "NONE");
  copyStatusField(deliveryCheckpoint, sizeof(deliveryCheckpoint), "NONE");
  copyStatusField(cargoStatus, sizeof(cargoStatus), "NONE");
  navigationDirection = NAVIGATION_FORWARD_ROUTE;
}

/**
 * @brief Marks mission cargo as picked up and pauses without blocking MQTT/Serial.
 */
void completePickupAtCheckpoint(const char* checkpoint) {
  motorController.stop();
  setCurrentCheckpoint(checkpoint);
  missionPhase = MISSION_PHASE_PICKUP_PAUSE;
  setRoverState("IDLE");
  rfidApproachUntilMs = 0;

  char pickedUpStatus[sizeof(cargoStatus)] = {};
  snprintf(pickedUpStatus, sizeof(pickedUpStatus), "PICKED_UP_%s", currentCargo);
  copyStatusField(cargoStatus, sizeof(cargoStatus), pickedUpStatus);

  Serial.print("PICKUP_COMPLETE:");
  Serial.println(currentCargo);

  char logMessage[48] = {};
  snprintf(logMessage, sizeof(logMessage), "PICKUP_COMPLETE:%s", currentCargo);
  publishLog(logMessage);
  publishRoverStatus();

  lineFollowPausedUntilMs = millis() + kPickupPauseDurationMs;
}

/**
 * @brief Completes the active RFID checkpoint mission at the delivery target.
 */
void completeMissionAtCheckpoint(const char* checkpoint) {
  motorController.stop();
  lineFollowEnabled = false;
  lineFollowTestActive = false;
  missionNavigationActive = false;
  missionPhase = MISSION_PHASE_DELIVERED;
  lineFollowPausedUntilMs = 0;
  rfidApproachUntilMs = 0;
  setCurrentCheckpoint(checkpoint);
  setRoverState("IDLE");

  if (strcmp(currentCargo, "NONE") == 0) {
    if (strcmp(checkpoint, kCheckpointBase) == 0) {
      copyStatusField(cargoStatus, sizeof(cargoStatus), "RETURNED_BASE");
    } else {
      copyStatusField(cargoStatus, sizeof(cargoStatus), "ARRIVED");
    }
  } else {
    char deliveredStatus[sizeof(cargoStatus)] = {};
    snprintf(deliveredStatus, sizeof(deliveredStatus), "DELIVERED_%s", currentCargo);
    copyStatusField(cargoStatus, sizeof(cargoStatus), deliveredStatus);
  }

  Serial.print("MISSION_COMPLETE:");
  Serial.println(currentMission);
  publishCommandLog("MISSION_COMPLETE:", currentMission);
  publishRoverStatus();
}

/**
 * @brief Resumes line following after the non-blocking pickup pause expires.
 */
void updateMissionPickupPause(const unsigned long now) {
  if (missionPhase != MISSION_PHASE_PICKUP_PAUSE) {
    return;
  }

  if (now < lineFollowPausedUntilMs) {
    motorController.stop();
    return;
  }

  lineFollowPausedUntilMs = 0;
  missionPhase = MISSION_PHASE_GOING_TO_DELIVERY;
  setTargetCheckpoint(deliveryCheckpoint);
  updateNavigationDirectionForTarget(deliveryCheckpoint);
  setRoverState("RUNNING");
  publishRoverStatus();
}

/**
 * @brief Checks for RFID checkpoints while autonomous line following is active.
 */
void scanRfidCheckpointDuringLineFollow(const unsigned long now) {
  if (!rfidReader.isInitialized() ||
      missionPhase == MISSION_PHASE_PICKUP_PAUSE ||
      now - lastLineFollowRfidScanMs < kRfidScanIntervalMs) {
    return;
  }
  lastLineFollowRfidScanMs = now;

  if (!rfidReader.isCardPresent()) {
    return;
  }

  captureLastRfidUid();
  const String detectedUid = rfidReader.readUID();
  const String normalizedUid = normalizeUid(detectedUid);
  const char* checkpoint = checkpointForUid(detectedUid);
  if (isDuplicateRfidRead(normalizedUid, checkpoint, now)) {
    return;
  }

  rememberRfidRead(normalizedUid, checkpoint, now);
  printRfidUidAndCheckpoint(detectedUid, checkpoint);

  Serial.print("RFID_CHECKPOINT:");
  Serial.println(checkpoint);
  setCurrentCheckpoint(checkpoint);

  if (!missionNavigationActive) {
    return;
  }

  if (strcmp(checkpoint, kCheckpointUnknown) == 0) {
    Serial.print("CONTINUE_TO:");
    Serial.println(targetCheckpoint);
    return;
  }

  if (missionPhase == MISSION_PHASE_GOING_TO_PICKUP &&
      strcmp(checkpoint, pickupCheckpoint) == 0) {
    completePickupAtCheckpoint(checkpoint);
    return;
  }

  if ((missionPhase == MISSION_PHASE_GOING_TO_DELIVERY ||
       missionPhase == MISSION_PHASE_RETURNING_BASE) &&
      strcmp(checkpoint, deliveryCheckpoint) == 0) {
    completeMissionAtCheckpoint(checkpoint);
    return;
  }

  Serial.print("CONTINUE_TO:");
  Serial.println(targetCheckpoint);
}

/**
 * @brief Runs one non-blocking line-following control step when scheduled.
 */
void updateLineFollower() {
  if (!lineFollowEnabled) {
    return;
  }

  const unsigned long now = millis();
  scanRfidCheckpointDuringLineFollow(now);
  if (!lineFollowEnabled) {
    return;
  }

  updateMissionPickupPause(now);
  if (missionPhase == MISSION_PHASE_PICKUP_PAUSE) {
    return;
  }

  if (lineFollowTestActive &&
      now - lineFollowTestStartedAt >= kLineFollowTestDurationMs) {
    disableLineFollowing(true);
    Serial.println("LINE_FOLLOW_TEST_COMPLETE");
    publishLog("LINE_FOLLOW_TEST_COMPLETE");
    publishRoverStatus();
    return;
  }

  if (lineFollowPausedUntilMs != 0 && now < lineFollowPausedUntilMs) {
    motorController.stop();
    return;
  }
  lineFollowPausedUntilMs = 0;

  if (now - lastLineFollowUpdateMs < kLineFollowIntervalMs) {
    return;
  }
  lastLineFollowUpdateMs = now;

  const LineSensorReadings readings = lineSensor.read();
  const bool leftDetected = readings.left == 1;
  const bool centerDetected = readings.center == 1;
  const bool rightDetected = readings.right == 1;
  const bool rfidApproachActive = now < rfidApproachUntilMs;

  if (!leftDetected && centerDetected && !rightDetected) {
    logLineFollowState(LINE_STATE_CENTER);
    lastLineDirection = LINE_DIR_CENTER;
    setRoverState("MOVING");
    if (navigationDirection == NAVIGATION_REVERSE_ROUTE) {
      motorController.backward(rfidApproachActive ? RFID_APPROACH_SPEED : LINE_FOLLOW_SPEED);
    } else {
      motorController.forward(rfidApproachActive ? RFID_APPROACH_SPEED : LINE_FOLLOW_SPEED);
    }
  } else if ((leftDetected && centerDetected && !rightDetected) ||
             (leftDetected && !centerDetected && !rightDetected)) {
    logLineFollowState(LINE_STATE_LEFT);
    lastLineDirection = LINE_DIR_LEFT;
    setRoverState("MOVING");
    if (navigationDirection == NAVIGATION_REVERSE_ROUTE) {
      motorController.right(LINE_CORRECTION_SPEED);
    } else {
      motorController.left(LINE_CORRECTION_SPEED);
    }
  } else if ((!leftDetected && centerDetected && rightDetected) ||
             (!leftDetected && !centerDetected && rightDetected)) {
    logLineFollowState(LINE_STATE_RIGHT);
    lastLineDirection = LINE_DIR_RIGHT;
    setRoverState("MOVING");
    if (navigationDirection == NAVIGATION_REVERSE_ROUTE) {
      motorController.left(LINE_CORRECTION_SPEED);
    } else {
      motorController.right(LINE_CORRECTION_SPEED);
    }
  } else if (leftDetected && centerDetected && rightDetected) {
    logLineFollowState(LINE_STATE_JUNCTION);
    setRoverState("MOVING");
    if (now >= rfidApproachUntilMs) {
      rfidApproachUntilMs = now + kRfidApproachScanDurationMs;
    }
    if (navigationDirection == NAVIGATION_REVERSE_ROUTE) {
      motorController.backward(RFID_APPROACH_SPEED);
    } else {
      motorController.forward(RFID_APPROACH_SPEED);
    }
  } else {
    logLineFollowState(LINE_STATE_LOST);
    motorController.stop();
    setRoverState("IDLE");
  }
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
         strcmp(command, "LINE_FOLLOW_START") == 0 ||
         strcmp(command, "LINE_FOLLOW_STOP") == 0 ||
         strcmp(command, "LINE_FOLLOW_TEST") == 0 ||
         strcmp(command, "RFID_TEST") == 0 ||
         strcmp(command, "SERVO_OPEN") == 0 ||
         strcmp(command, "SERVO_CLOSE") == 0 ||
         strcmp(command, "SERVO_TEST") == 0 ||
         strcmp(command, "SELF_TEST") == 0 ||
         strcmp(command, "MISSION_1") == 0 ||
         strcmp(command, "MISSION_2") == 0 ||
         strcmp(command, "MISSION_3") == 0 ||
         strcmp(command, "MISSION_4") == 0 ||
         strcmp(command, "MISSION_5") == 0 ||
         strcmp(command, "RETURN_BASE") == 0;
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
  disableLineFollowing(true);
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
 * @brief Starts straight-line RFID checkpoint navigation for a mission.
 *
 * The rover follows the black line continuously and scans RFID tags until the
 * detected checkpoint matches the mission target. This function intentionally
 * returns immediately so MQTT and Serial processing remain responsive.
 *
 * @param command Mission command from MISSION_1 through MISSION_5.
 */
void runMission(const char* command) {
  const char* missionCommand = strcmp(command, "RETURN_BASE") == 0 ? "MISSION_5" : command;
  motorController.stop();
  setCurrentMission(missionCommand);
  copyStatusField(currentCargo, sizeof(currentCargo), cargoForMission(missionCommand));
  copyStatusField(pickupCheckpoint,
                  sizeof(pickupCheckpoint),
                  pickupCheckpointForMission(missionCommand));
  copyStatusField(deliveryCheckpoint,
                  sizeof(deliveryCheckpoint),
                  deliveryCheckpointForMission(missionCommand));
  copyStatusField(cargoStatus,
                  sizeof(cargoStatus),
                  strcmp(currentCargo, "NONE") == 0 ? "NO_CARGO" : "AWAITING_PICKUP");

  if (strcmp(missionCommand, "MISSION_5") == 0) {
    missionPhase = MISSION_PHASE_RETURNING_BASE;
    setTargetCheckpoint(deliveryCheckpoint);
  } else if (strcmp(pickupCheckpoint, "NONE") == 0) {
    missionPhase = MISSION_PHASE_GOING_TO_DELIVERY;
    setTargetCheckpoint(deliveryCheckpoint);
  } else {
    missionPhase = MISSION_PHASE_GOING_TO_PICKUP;
    setTargetCheckpoint(pickupCheckpoint);
  }
  const bool alreadyAtTarget = updateNavigationDirectionForTarget(targetCheckpoint);

  missionNavigationActive = true;
  lineFollowEnabled = true;
  lineFollowTestActive = false;
  lineFollowTestStartedAt = 0;
  lastLineFollowUpdateMs = 0;
  lastLineFollowRfidScanMs = 0;
  lineFollowPausedUntilMs = 0;
  rfidApproachUntilMs = 0;
  currentLineFollowState = LINE_STATE_UNKNOWN;
  lastLineDirection = LINE_DIR_CENTER;
  setRoverState("RUNNING");

  Serial.print("MISSION_START:");
  Serial.println(missionCommand);
  Serial.print("TARGET_CHECKPOINT:");
  Serial.println(targetCheckpoint);
  publishCommandLog("MISSION_START:", missionCommand);
  publishLog("LINE_FOLLOW_STARTED");
  publishRoverStatus();

  if (alreadyAtTarget) {
    if (missionPhase == MISSION_PHASE_GOING_TO_PICKUP) {
      completePickupAtCheckpoint(currentCheckpoint);
    } else {
      completeMissionAtCheckpoint(currentCheckpoint);
    }
  }
}

/**
 * @brief Samples the BFD-1000 every 300 ms for a total of 20 seconds.
 *
 * Motors are stopped before diagnostics begin so sensor testing cannot leave
 * the rover moving. Sampling is scheduled from the original start time so
 * serial printing does not accumulate drift between samples.
 */
void runLineTest() {
  disableLineFollowing(true);
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
 * once using its normalized hexadecimal UID and mapped checkpoint.
 */
void runRfidTest() {
  disableLineFollowing(true);
  motorController.stop();
  const unsigned long startTime = millis();
  char testLastUid[24] = "";
  unsigned long testLastPrintedAtMs = 0;

  while (millis() - startTime < kRfidTestDurationMs) {
    if (rfidReader.isCardPresent()) {
      captureLastRfidUid();
      const String detectedUid = rfidReader.readUID();
      const String normalizedUid = normalizeUid(detectedUid);
      const unsigned long now = millis();
      if (now - testLastPrintedAtMs >= kRfidDebounceMs ||
          strcmp(normalizedUid.c_str(), testLastUid) != 0) {
        printRfidUidAndCheckpoint(detectedUid, checkpointForUid(detectedUid));
        normalizedUid.toCharArray(testLastUid, sizeof(testLastUid));
        testLastPrintedAtMs = now;
      }
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
  disableLineFollowing(true);
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
    disableLineFollowing(false);
    setRoverState("MOVING");
    motorController.forward();
  } else if (strcmp(command, "BACKWARD") == 0) {
    disableLineFollowing(false);
    setRoverState("MOVING");
    motorController.backward();
  } else if (strcmp(command, "LEFT") == 0) {
    disableLineFollowing(false);
    setRoverState("MOVING");
    motorController.left();
  } else if (strcmp(command, "RIGHT") == 0) {
    disableLineFollowing(false);
    setRoverState("MOVING");
    motorController.right();
  } else if (strcmp(command, "STOP") == 0) {
    disableLineFollowing(true);
  } else if (strcmp(command, "TEST") == 0) {
    runMotorTest();
  } else if (strcmp(command, "LINE_TEST") == 0) {
    Serial.println("ACK:LINE_TEST");
    runLineTest();
    return;
  } else if (strcmp(command, "LINE_FOLLOW_START") == 0) {
    startLineFollowing();
    return;
  } else if (strcmp(command, "LINE_FOLLOW_STOP") == 0) {
    disableLineFollowing(true);
    Serial.println("ACK:LINE_FOLLOW_STOP");
    publishLog("LINE_FOLLOW_STOPPED");
    publishRoverStatus();
    return;
  } else if (strcmp(command, "LINE_FOLLOW_TEST") == 0) {
    startLineFollowTest();
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
             strcmp(command, "MISSION_5") == 0 ||
             strcmp(command, "RETURN_BASE") == 0) {
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
  const char* lineState = lineStatusName(currentLineFollowState);
  const char* lineFollowState = lineFollowStateName(currentLineFollowState);
  const char* lineFollowMode = lineFollowEnabled ? "enabled" : "disabled";

  const long wifiRssi = mqttManager.isWifiConnected() ? WiFi.RSSI() : 0;
  const unsigned long uptimeSeconds = millis() / 1000;

  char statusPayload[896] = {};
  snprintf(statusPayload,
           sizeof(statusPayload),
           "{\"rover_id\":\"%s\",\"firmware\":\"v0.9\",\"mission\":\"%s\","
           "\"mission_phase\":\"%s\",\"mission_complete\":%s,"
           "\"navigation_direction\":\"%s\","
           "\"state\":\"%s\",\"location\":\"%s\","
           "\"cargo\":\"%s\",\"cargo_status\":\"%s\","
           "\"pickup_checkpoint\":\"%s\",\"delivery_checkpoint\":\"%s\","
           "\"target_checkpoint\":\"%s\","
           "\"current_checkpoint\":\"%s\",\"battery\":87,"
           "\"wifi_rssi\":%ld,\"uptime\":%lu,\"line\":\"%s\","
           "\"line_follow\":\"%s\",\"line_state\":\"%s\","
           "\"rfid\":\"%s\",\"servo\":\"%s\",\"timestamp\":%lu}",
           ROVER_ID,
           currentMission,
           missionPhaseName(missionPhase),
           missionPhase == MISSION_PHASE_DELIVERED ? "true" : "false",
           navigationDirectionName(navigationDirection),
           roverState,
           roverLocation,
           currentCargo,
           cargoStatus,
           pickupCheckpoint,
           deliveryCheckpoint,
           targetCheckpoint,
           currentCheckpoint,
           wifiRssi,
           uptimeSeconds,
           lineState,
           lineFollowMode,
           lineFollowState,
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
    disableLineFollowing(true);
    setRoverState("ERROR");
    Serial.println("ERR:WIFI_DISCONNECTED");
  }

  if (wasMqttConnected && !mqttConnected) {
    disableLineFollowing(true);
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
 * sequence, RFID checkpoint missions, line diagnostics, RFID diagnostics,
 * cargo-servo diagnostics, and the combined hardware self-test are available
 * on demand.
 */
void loop() {
  processNetwork();
  processSerialInput();
  processMqttCommand();
  updateLineFollower();
}
