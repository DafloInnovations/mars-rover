#ifndef MISSION_TO_MARS_CARGO_SERVO_H
#define MISSION_TO_MARS_CARGO_SERVO_H

#include <Arduino.h>
#include <ESP32Servo.h>

/**
 * @brief Controls the SG90 cargo mechanism servo.
 *
 * The signal pin and travel positions are constructor-injected so this module
 * remains independent of the rover's board wiring and mechanical calibration.
 */
class CargoServo {
 public:
  /**
   * @brief Creates a cargo servo with caller-supplied configuration.
   *
   * @param signalPin ESP32 GPIO connected to the SG90 signal wire.
   * @param closedAngle Mechanical closed position in degrees.
   * @param openAngle Mechanical open position in degrees.
   */
  CargoServo(uint8_t signalPin,
             uint8_t closedAngle = 0,
             uint8_t openAngle = 90);

  /**
   * @brief Allocates an ESP32 PWM timer, attaches the servo, and closes it.
   */
  void begin();

  /**
   * @brief Moves the cargo mechanism to its configured open position.
   */
  void open();

  /**
   * @brief Moves the cargo mechanism to its configured closed position.
   */
  void close();

  /**
   * @brief Opens the mechanism, waits one second, and closes it.
   */
  void test();

  /**
   * @brief Reports whether the servo successfully attached to a PWM channel.
   */
  bool isInitialized() const;

 private:
  static constexpr int kMinimumPulseWidthUs = 500;
  static constexpr int kMaximumPulseWidthUs = 2400;
  static constexpr unsigned long kTestHoldDurationMs = 1000;

  const uint8_t signalPin_;
  const uint8_t closedAngle_;
  const uint8_t openAngle_;
  Servo servo_;
  bool initialized_ = false;
};

#endif  // MISSION_TO_MARS_CARGO_SERVO_H
