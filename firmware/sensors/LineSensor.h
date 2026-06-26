#ifndef MISSION_TO_MARS_LINE_SENSOR_H
#define MISSION_TO_MARS_LINE_SENSOR_H

#include <Arduino.h>

/**
 * @brief Five-channel digital reading returned by the BFD-1000 sensor.
 */
struct LineSensorReadings {
  uint8_t s1;
  uint8_t s2;
  uint8_t s3;
  uint8_t s4;
  uint8_t s5;
};

/**
 * @brief Reads and reports a five-channel BFD-1000 line sensor.
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
   * @param s1Pin GPIO connected to sensor channel S1.
   * @param s2Pin GPIO connected to sensor channel S2.
   * @param s3Pin GPIO connected to sensor channel S3.
   * @param s4Pin GPIO connected to sensor channel S4.
   * @param s5Pin GPIO connected to sensor channel S5.
   * @param blackState Digital state emitted when a channel detects black.
   */
  LineSensor(uint8_t s1Pin,
             uint8_t s2Pin,
             uint8_t s3Pin,
             uint8_t s4Pin,
             uint8_t s5Pin,
             uint8_t blackState = HIGH);

  /**
   * @brief Configures all five sensor channels as digital inputs.
   *
   * Plain INPUT mode is required because ESP32 GPIO34, GPIO35, and GPIO39 do
   * not provide internal pull-up or pull-down resistors.
   */
  void begin();

  /**
   * @brief Samples all five digital sensor channels.
   *
   * @return Snapshot containing normalized zero-or-one readings.
   */
  LineSensorReadings read() const;

  /**
   * @brief Prints one sample in the Mission Control diagnostic format.
   */
  void printReadings() const;

  /**
   * @brief Reports whether every channel currently detects black.
   *
   * @return true when S1 through S5 all equal the configured black state.
   */
  bool isJunction() const;

  /**
   * @brief Reports whether begin() has configured all sensor inputs.
   */
  bool isInitialized() const;

 private:
  const uint8_t s1Pin_;
  const uint8_t s2Pin_;
  const uint8_t s3Pin_;
  const uint8_t s4Pin_;
  const uint8_t s5Pin_;
  const uint8_t blackState_;
  bool initialized_ = false;
};

#endif  // MISSION_TO_MARS_LINE_SENSOR_H
