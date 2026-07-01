#include "LineSensor.h"

LineSensor::LineSensor(const uint8_t leftPin,
                       const uint8_t centerPin,
                       const uint8_t rightPin,
                       const uint8_t blackState)
    : leftPin_(leftPin),
      centerPin_(centerPin),
      rightPin_(rightPin),
      blackState_(blackState) {}

/**
 * @brief Initializes each line-sensor channel as a digital input.
 */
void LineSensor::begin() {
  pinMode(leftPin_, INPUT);
  pinMode(centerPin_, INPUT);
  pinMode(rightPin_, INPUT);
  initialized_ = true;
}

/**
 * @brief Captures one coherent snapshot of black-detection states.
 */
LineSensorReadings LineSensor::read() const {
  return {
      static_cast<uint8_t>(digitalRead(leftPin_) == blackState_),
      static_cast<uint8_t>(digitalRead(centerPin_) == blackState_),
      static_cast<uint8_t>(digitalRead(rightPin_) == blackState_),
  };
}

/**
 * @brief Emits the current readings as one machine-readable serial line.
 */
void LineSensor::printReadings() const {
  const LineSensorReadings readings = read();

  Serial.print("LINE:L=");
  Serial.print(readings.left);
  Serial.print(",C=");
  Serial.print(readings.center);
  Serial.print(",R=");
  Serial.println(readings.right);
}

/**
 * @brief Tests the latest channel states against the configured black level.
 */
bool LineSensor::isJunction() const {
  const LineSensorReadings readings = read();

  return readings.left == 1 &&
         readings.center == 1 &&
         readings.right == 1;
}

/**
 * @brief Returns the line-sensor GPIO initialization state.
 */
bool LineSensor::isInitialized() const {
  return initialized_;
}
