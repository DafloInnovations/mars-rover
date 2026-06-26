#include "RFIDReader.h"

#include <SPI.h>

RFIDReader::RFIDReader(const uint8_t ssPin,
                       const uint8_t resetPin,
                       const uint8_t clockPin,
                       const uint8_t mosiPin,
                       const uint8_t misoPin)
    : ssPin_(ssPin),
      resetPin_(resetPin),
      clockPin_(clockPin),
      mosiPin_(mosiPin),
      misoPin_(misoPin),
      reader_(ssPin, resetPin) {}

/**
 * @brief Starts the ESP32 SPI peripheral and initializes the RC522.
 */
void RFIDReader::begin() {
  SPI.begin(clockPin_, misoPin_, mosiPin_, ssPin_);
  reader_.PCD_Init();
  const byte version = reader_.PCD_ReadRegister(MFRC522::VersionReg);
  initialized_ = version != 0x00 && version != 0xFF;
}

/**
 * @brief Detects a new PICC, reads its serial number, and stores the UID.
 */
bool RFIDReader::isCardPresent() {
  if (!reader_.PICC_IsNewCardPresent() || !reader_.PICC_ReadCardSerial()) {
    return false;
  }

  uidLength_ = min(reader_.uid.size, kMaximumUidSize);
  for (byte index = 0; index < uidLength_; ++index) {
    uid_[index] = reader_.uid.uidByte[index];
  }

  reader_.PICC_HaltA();
  reader_.PCD_StopCrypto1();
  return true;
}

/**
 * @brief Converts the captured UID bytes to a compact hexadecimal string.
 */
String RFIDReader::readUID() const {
  String uid;
  uid.reserve(uidLength_ * 2);

  for (byte index = 0; index < uidLength_; ++index) {
    if (uid_[index] < 0x10) {
      uid += '0';
    }
    uid += String(uid_[index], HEX);
  }

  uid.toUpperCase();
  return uid;
}

/**
 * @brief Emits the captured UID using the firmware serial protocol.
 */
void RFIDReader::printUID() const {
  Serial.print("RFID:UID=");
  Serial.println(readUID());
}

/**
 * @brief Returns whether the RC522 responded during initialization.
 */
bool RFIDReader::isInitialized() const {
  return initialized_;
}
