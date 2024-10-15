#ifndef SERIALIR_HPP
#define SERIALIR_HPP

#include <stdint.h>
#include <SoftwareSerial.h>                                         /// Arduino software serial lib.

class SerialIR final : public SoftwareSerial {
public:
  SerialIR(uint8_t RXpin, uint8_t TXpin) : SoftwareSerial(RXpin, TXpin) {
    SoftwareSerial::begin(9600);
  }

  virtual ~SerialIR() = default;
  size_t write(const uint8_t* buffer, size_t size) {
    constexpr uint8_t sendBaseValues[] = { 0xA1, 0xF1 };
    size_t retValue = 0;
    retValue += SoftwareSerial::write(sendBaseValues, sizeof(sendBaseValues));
    retValue += SoftwareSerial::write(buffer, size);
    return retValue;
  }
  SerialIR(const SerialIR&) = delete;                       // Define copy constructor.
  SerialIR& operator=(const SerialIR&) = delete;            // Define copy assignment operator.
  SerialIR(SerialIR&&) = delete;                            // Define move constructor.
  SerialIR& operator=(SerialIR&&) = delete;                 // Define move assignment operator.
private:
};
#endif  // SERIALIR_HPP