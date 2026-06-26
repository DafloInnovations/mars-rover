#ifndef MISSION_TO_MARS_RFID_READER_H
#define MISSION_TO_MARS_RFID_READER_H

#include <Arduino.h>
#include <MFRC522.h>

/**
 * @brief Controls an RC522 RFID reader connected to the ESP32 SPI bus.
 *
 * All SPI and control pins are supplied through the constructor so the class
 * contains no board-specific GPIO assignments.
 */
class RFIDReader {
 public:
  /**
   * @brief Creates an RC522 reader with caller-provided ESP32 pins.
   *
   * @param ssPin SPI chip-select pin, labelled SDA or SS on RC522 modules.
   * @param resetPin RC522 reset pin.
   * @param clockPin SPI clock pin.
   * @param mosiPin SPI controller-out/peripheral-in pin.
   * @param misoPin SPI controller-in/peripheral-out pin.
   */
  RFIDReader(uint8_t ssPin,
             uint8_t resetPin,
             uint8_t clockPin,
             uint8_t mosiPin,
             uint8_t misoPin);

  /**
   * @brief Initializes the configured SPI bus and RC522 reader.
   */
  void begin();

  /**
   * @brief Checks for a new card and captures its UID.
   *
   * @return true when a complete UID was read from a newly presented card.
   */
  bool isCardPresent();

  /**
   * @brief Returns the most recently captured UID as uppercase hexadecimal.
   *
   * @return UID without separators, or an empty string before the first read.
   */
  String readUID() const;

  /**
   * @brief Prints the most recently captured UID in diagnostic protocol format.
   */
  void printUID() const;

  /**
   * @brief Reports whether the RC522 responded with a valid version register.
   */
  bool isInitialized() const;

 private:
  static constexpr byte kMaximumUidSize = 10;

  const uint8_t ssPin_;
  const uint8_t resetPin_;
  const uint8_t clockPin_;
  const uint8_t mosiPin_;
  const uint8_t misoPin_;
  MFRC522 reader_;
  byte uid_[kMaximumUidSize] = {};
  byte uidLength_ = 0;
  bool initialized_ = false;
};

#endif  // MISSION_TO_MARS_RFID_READER_H
