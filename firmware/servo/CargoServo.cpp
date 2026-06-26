#include "CargoServo.h"

CargoServo::CargoServo(const uint8_t signalPin,
                       const uint8_t closedAngle,
                       const uint8_t openAngle)
    : signalPin_(signalPin),
      closedAngle_(closedAngle),
      openAngle_(openAngle) {}

/**
 * @brief Initializes PWM output and establishes the safe closed position.
 */
void CargoServo::begin() {
  ESP32PWM::allocateTimer(0);
  servo_.setPeriodHertz(50);
  initialized_ =
      servo_.attach(signalPin_, kMinimumPulseWidthUs, kMaximumPulseWidthUs) > 0;
  if (initialized_) {
    close();
  }
}

/**
 * @brief Commands the SG90 to the configured 90-degree open position.
 */
void CargoServo::open() {
  servo_.write(openAngle_);
}

/**
 * @brief Commands the SG90 to the configured zero-degree closed position.
 */
void CargoServo::close() {
  servo_.write(closedAngle_);
}

/**
 * @brief Performs one complete open-and-close diagnostic cycle.
 */
void CargoServo::test() {
  open();
  delay(kTestHoldDurationMs);
  close();
}

/**
 * @brief Returns the PWM attachment state.
 */
bool CargoServo::isInitialized() const {
  return initialized_;
}
