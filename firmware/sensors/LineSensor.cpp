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
 * @brief Captures one coherent snapshot of all line-sensor channels.
 */
LineSensorReadings LineSensor::read() const {
  return {
      static_cast<uint8_t>(digitalRead(leftPin_) == HIGH),
      static_cast<uint8_t>(digitalRead(centerPin_) == HIGH),
      static_cast<uint8_t>(digitalRead(rightPin_) == HIGH),
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
  const uint8_t normalizedBlackState =
      static_cast<uint8_t>(blackState_ == HIGH);

  return readings.left == normalizedBlackState &&
         readings.center == normalizedBlackState &&
         readings.right == normalizedBlackState;
}

/**
 * @brief Returns the line-sensor GPIO initialization state.
 */
bool LineSensor::isInitialized() const {
  return initialized_;
}
