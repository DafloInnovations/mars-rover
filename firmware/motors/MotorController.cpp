#include "MotorController.h"

/**
 * @brief Stores the caller-provided H-bridge GPIO assignments.
 *
 * Hardware configuration remains outside this class, allowing the same motor
 * logic to be used with different ESP32 boards and wiring layouts.
 */
MotorController::MotorController(const uint8_t leftMotorInput1,
                                 const uint8_t leftMotorInput2,
                                 const uint8_t rightMotorInput1,
                                 const uint8_t rightMotorInput2)
    : leftMotorInput1_(leftMotorInput1),
      leftMotorInput2_(leftMotorInput2),
      rightMotorInput1_(rightMotorInput1),
      rightMotorInput2_(rightMotorInput2) {}

/**
 * @brief Initializes the motor-control outputs in a safe stopped state.
 *
 * Stopping immediately after pin configuration prevents unintended movement
 * while the remainder of the rover firmware starts.
 */
void MotorController::begin() {
  pinMode(leftMotorInput1_, OUTPUT);
  pinMode(leftMotorInput2_, OUTPUT);
  pinMode(rightMotorInput1_, OUTPUT);
  pinMode(rightMotorInput2_, OUTPUT);

  stop();
  initialized_ = true;
}

/**
 * @brief Commands both motors to rotate in the forward direction.
 */
void MotorController::forward() {
  forward(255);
}

/**
 * @brief Commands both motors to rotate forward with PWM speed control.
 */
void MotorController::forward(const uint8_t speed) {
  setMotorStates(HIGH, LOW, HIGH, LOW, speed);
}

/**
 * @brief Commands both motors to rotate in the reverse direction.
 */
void MotorController::backward() {
  backward(255);
}

/**
 * @brief Commands both motors to rotate backward with PWM speed control.
 */
void MotorController::backward(const uint8_t speed) {
  setMotorStates(LOW, HIGH, LOW, HIGH, speed);
}

/**
 * @brief Commands an in-place left pivot.
 *
 * The left motor reverses while the right motor advances.
 */
void MotorController::left() {
  left(255);
}

/**
 * @brief Commands an in-place left pivot with PWM speed control.
 */
void MotorController::left(const uint8_t speed) {
  setMotorStates(LOW, HIGH, HIGH, LOW, speed);
}

/**
 * @brief Commands an in-place right pivot.
 *
 * The left motor advances while the right motor reverses.
 */
void MotorController::right() {
  right(255);
}

/**
 * @brief Commands an in-place right pivot with PWM speed control.
 */
void MotorController::right(const uint8_t speed) {
  setMotorStates(HIGH, LOW, LOW, HIGH, speed);
}

/**
 * @brief Stops motor drive by setting all H-bridge inputs LOW.
 *
 * Depending on the selected H-bridge, this state normally coasts the motors.
 * Consult the motor-driver data sheet to confirm its stop behavior.
 */
void MotorController::stop() {
  setMotorStates(LOW, LOW, LOW, LOW);
}

/**
 * @brief Returns the motor GPIO initialization state.
 */
bool MotorController::isInitialized() const {
  return initialized_;
}

/**
 * @brief Writes a complete direction state to the dual H-bridge.
 *
 * Keeping all digital writes in this helper makes the public movement methods
 * concise and provides one place to audit the low-level motor behavior.
 */
void MotorController::setMotorStates(const uint8_t leftInput1State,
                                     const uint8_t leftInput2State,
                                     const uint8_t rightInput1State,
                                     const uint8_t rightInput2State) {
  setMotorStates(leftInput1State,
                 leftInput2State,
                 rightInput1State,
                 rightInput2State,
                 255);
}

/**
 * @brief Writes a complete direction state with PWM speed control.
 */
void MotorController::setMotorStates(const uint8_t leftInput1State,
                                     const uint8_t leftInput2State,
                                     const uint8_t rightInput1State,
                                     const uint8_t rightInput2State,
                                     const uint8_t speed) {
  writeMotorInput(leftMotorInput1_, leftInput1State, speed);
  writeMotorInput(leftMotorInput2_, leftInput2State, speed);
  writeMotorInput(rightMotorInput1_, rightInput1State, speed);
  writeMotorInput(rightMotorInput2_, rightInput2State, speed);
}

/**
 * @brief Drives inactive H-bridge inputs LOW and active inputs with PWM.
 */
void MotorController::writeMotorInput(const uint8_t pin,
                                      const uint8_t state,
                                      const uint8_t speed) {
  if (state == LOW || speed == 0) {
    digitalWrite(pin, LOW);
    return;
  }

  if (speed >= 255) {
    digitalWrite(pin, HIGH);
    return;
  }

  analogWrite(pin, speed);
}
