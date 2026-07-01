#ifndef MISSION_TO_MARS_MOTOR_CONTROLLER_H
#define MISSION_TO_MARS_MOTOR_CONTROLLER_H

#include <Arduino.h>

/**
 * @brief Controls a two-wheel differential-drive rover through an H-bridge.
 *
 * The controller owns all motor direction logic but does not choose the ESP32
 * GPIO pins. Pin assignments are injected through the constructor, keeping the
 * class reusable across different rover wiring configurations.
 */
class MotorController {
 public:
  /**
   * @brief Creates a motor controller using externally supplied GPIO pins.
   *
   * @param leftMotorInput1  H-bridge input 1 for the left motor.
   * @param leftMotorInput2  H-bridge input 2 for the left motor.
   * @param rightMotorInput1 H-bridge input 1 for the right motor.
   * @param rightMotorInput2 H-bridge input 2 for the right motor.
   */
  MotorController(uint8_t leftMotorInput1,
                  uint8_t leftMotorInput2,
                  uint8_t rightMotorInput1,
                  uint8_t rightMotorInput2);

  /**
   * @brief Configures all motor GPIO pins as outputs and stops both motors.
   *
   * Call this method once from Arduino setup() before issuing movement
   * commands.
   */
  void begin();

  /**
   * @brief Drives both motors forward.
   */
  void forward();

  /**
   * @brief Drives both motors forward using the requested PWM speed.
   *
   * @param speed PWM duty cycle from 0 stopped to 255 full speed.
   */
  void forward(uint8_t speed);

  /**
   * @brief Drives both motors backward.
   */
  void backward();

  /**
   * @brief Drives both motors backward using the requested PWM speed.
   *
   * @param speed PWM duty cycle from 0 stopped to 255 full speed.
   */
  void backward(uint8_t speed);

  /**
   * @brief Pivots the rover left by reversing the left motor and advancing the
   * right motor.
   */
  void left();

  /**
   * @brief Pivots the rover left using the requested PWM speed.
   *
   * @param speed PWM duty cycle from 0 stopped to 255 full speed.
   */
  void left(uint8_t speed);

  /**
   * @brief Pivots the rover right by advancing the left motor and reversing the
   * right motor.
   */
  void right();

  /**
   * @brief Pivots the rover right using the requested PWM speed.
   *
   * @param speed PWM duty cycle from 0 stopped to 255 full speed.
   */
  void right(uint8_t speed);

  /**
   * @brief Stops both motors by driving every H-bridge input LOW.
   */
  void stop();

  /**
   * @brief Reports whether begin() has configured the motor outputs.
   */
  bool isInitialized() const;

 private:
  /**
   * @brief Applies direction states to both motors in one operation.
   *
   * @param leftInput1State  Logic state for left motor input 1.
   * @param leftInput2State  Logic state for left motor input 2.
   * @param rightInput1State Logic state for right motor input 1.
   * @param rightInput2State Logic state for right motor input 2.
   */
  void setMotorStates(uint8_t leftInput1State,
                      uint8_t leftInput2State,
                      uint8_t rightInput1State,
                      uint8_t rightInput2State);

  /**
   * @brief Applies direction states with PWM on active H-bridge inputs.
   *
   * @param leftInput1State  Logic state for left motor input 1.
   * @param leftInput2State  Logic state for left motor input 2.
   * @param rightInput1State Logic state for right motor input 1.
   * @param rightInput2State Logic state for right motor input 2.
   * @param speed PWM duty cycle from 0 stopped to 255 full speed.
   */
  void setMotorStates(uint8_t leftInput1State,
                      uint8_t leftInput2State,
                      uint8_t rightInput1State,
                      uint8_t rightInput2State,
                      uint8_t speed);

  /**
   * @brief Writes either LOW or a PWM duty cycle to one H-bridge input pin.
   */
  void writeMotorInput(uint8_t pin, uint8_t state, uint8_t speed);

  const uint8_t leftMotorInput1_;
  const uint8_t leftMotorInput2_;
  const uint8_t rightMotorInput1_;
  const uint8_t rightMotorInput2_;
  bool initialized_ = false;
};

#endif  // MISSION_TO_MARS_MOTOR_CONTROLLER_H
