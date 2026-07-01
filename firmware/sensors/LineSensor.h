#ifndef MISSION_TO_MARS_LINE_SENSOR_H
#define MISSION_TO_MARS_LINE_SENSOR_H

#include <Arduino.h>

/**
 * @brief Three-channel normalized black-detection reading.
 *
 * A value of 1 means that channel is currently detecting black, regardless of
 * whether the physical sensor module drives LOW or HIGH for black.
 */
struct LineSensorReadings {
  uint8_t left;
  uint8_t center;
  uint8_t right;
};

/**
 * @brief Reads and reports a three-channel line sensor.
 *
 * GPIO assignments and the electrical state representing black are supplied
 * through the constructor, keeping this class independent of rover wiring and
 * sensor-module polarity.
 */
class LineSensor {
 public:
  /**
   * @brief Creates a line sensor using externally supplied GPIO assignments.
   *
   * @param leftPin GPIO connected to the left line sensor output.
   * @param centerPin GPIO connected to the center line sensor output.
   * @param rightPin GPIO connected to the right line sensor output.
   * @param blackState Digital state emitted when a channel detects black.
   */
  LineSensor(uint8_t leftPin,
             uint8_t centerPin,
             uint8_t rightPin,
             uint8_t blackState = HIGH);

  /**
   * @brief Configures all three sensor channels as digital inputs.
   */
  void begin();

  /**
   * @brief Samples all three digital sensor channels.
   *
   * @return Snapshot containing normalized readings where 1 means black detected.
   */
  LineSensorReadings read() const;

  /**
   * @brief Prints one sample in the Mission Control diagnostic format.
   */
  void printReadings() const;

  /**
   * @brief Reports whether every channel currently detects black.
   *
   * @return true when left, center, and right all equal the configured black state.
   */
  bool isJunction() const;

  /**
   * @brief Reports whether begin() has configured all sensor inputs.
   */
  bool isInitialized() const;

 private:
  const uint8_t leftPin_;
  const uint8_t centerPin_;
  const uint8_t rightPin_;
  const uint8_t blackState_;
  bool initialized_ = false;
};

#endif  // MISSION_TO_MARS_LINE_SENSOR_H
