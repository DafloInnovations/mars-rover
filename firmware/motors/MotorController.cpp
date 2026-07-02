#include "MotorController.h"

namespace {
// Reduce motor power to ~30% of full output (70% reduction) while preserving
// the existing forward/backward/left/right command behavior.
constexpr uint8_t kMotorPwmDuty = 76;
}

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
  setMotorStates(HIGH, LOW, HIGH, LOW);
}

/**
 * @brief Commands both motors to rotate in the reverse direction.
 */
void MotorController::backward() {
  setMotorStates(LOW, HIGH, LOW, HIGH);
}

/**
 * @brief Commands an in-place left pivot.
 *
 * The left motor reverses while the right motor advances.
 */
void MotorController::left() {
  setMotorStates(LOW, HIGH, HIGH, LOW);
}

/**
 * @brief Commands an in-place right pivot.
 *
 * The left motor advances while the right motor reverses.
 */
void MotorController::right() {
  setMotorStates(HIGH, LOW, LOW, HIGH);
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
  analogWrite(leftMotorInput1_, leftInput1State == LOW ? 0 : kMotorPwmDuty);
  analogWrite(leftMotorInput2_, leftInput2State == LOW ? 0 : kMotorPwmDuty);
  analogWrite(rightMotorInput1_, rightInput1State == LOW ? 0 : kMotorPwmDuty);
  analogWrite(rightMotorInput2_, rightInput2State == LOW ? 0 : kMotorPwmDuty);
}
