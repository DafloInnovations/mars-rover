#include "LineSensor.h"

LineSensor::LineSensor(const uint8_t s1Pin,
                       const uint8_t s2Pin,
                       const uint8_t s3Pin,
                       const uint8_t s4Pin,
                       const uint8_t s5Pin,
                       const uint8_t blackState)
    : s1Pin_(s1Pin),
      s2Pin_(s2Pin),
      s3Pin_(s3Pin),
      s4Pin_(s4Pin),
      s5Pin_(s5Pin),
      blackState_(blackState) {}

/**
 * @brief Initializes each BFD-1000 channel as a digital input.
 */
void LineSensor::begin() {
  pinMode(s1Pin_, INPUT);
  pinMode(s2Pin_, INPUT);
  pinMode(s3Pin_, INPUT);
  pinMode(s4Pin_, INPUT);
  pinMode(s5Pin_, INPUT);
  initialized_ = true;
}

/**
 * @brief Captures one coherent snapshot of all line-sensor channels.
 */
LineSensorReadings LineSensor::read() const {
  return {
      static_cast<uint8_t>(digitalRead(s1Pin_) == HIGH),
      static_cast<uint8_t>(digitalRead(s2Pin_) == HIGH),
      static_cast<uint8_t>(digitalRead(s3Pin_) == HIGH),
      static_cast<uint8_t>(digitalRead(s4Pin_) == HIGH),
      static_cast<uint8_t>(digitalRead(s5Pin_) == HIGH),
  };
}

/**
 * @brief Emits the current readings as one machine-readable serial line.
 */
void LineSensor::printReadings() const {
  const LineSensorReadings readings = read();

  Serial.print("LINE:S1=");
  Serial.print(readings.s1);
  Serial.print(",S2=");
  Serial.print(readings.s2);
  Serial.print(",S3=");
  Serial.print(readings.s3);
  Serial.print(",S4=");
  Serial.print(readings.s4);
  Serial.print(",S5=");
  Serial.println(readings.s5);
}

/**
 * @brief Tests the latest channel states against the configured black level.
 */
bool LineSensor::isJunction() const {
  const LineSensorReadings readings = read();
  const uint8_t normalizedBlackState =
      static_cast<uint8_t>(blackState_ == HIGH);

  return readings.s1 == normalizedBlackState &&
         readings.s2 == normalizedBlackState &&
         readings.s3 == normalizedBlackState &&
         readings.s4 == normalizedBlackState &&
         readings.s5 == normalizedBlackState;
}

/**
 * @brief Returns the line-sensor GPIO initialization state.
 */
bool LineSensor::isInitialized() const {
  return initialized_;
}
