#ifndef PCF8574_HPP
#define PCF8574_HPP

#include <stdint.h>
#include <Wire.h>

class PCF8574 final {
public:
  enum class PinDir : uint8_t {
    IN = 0,
    OUT
  };

  enum class PinState : uint8_t {
    L = 0,    // Low.
    H,        // High.
    E         // Error.
  };

  enum class Pin : uint8_t {
    D0 = 0,
    D1,
    D2,
    D3,
    D4,
    D5,
    D6,
    D7
  };

  PCF8574(uint8_t address, TwoWire &wire = Wire);
  ~PCF8574() = default;

  bool begin();
  bool write(uint8_t reg);
  bool read(uint8_t &value);
  uint8_t getRegisterValue();

  bool setAsInput(Pin pin);
  bool digitalWrite(Pin pin, PinState pinState);
  PinState digitalRead(Pin pin);
  bool toggleState(Pin pin);

  PCF8574(const PCF8574&) = delete;               // Copy constructor deleted
  PCF8574& operator=(const PCF8574&) = delete;    // Copy assignment operator deleted
  PCF8574(PCF8574&&) = delete;                    // Move constructor deleted
  PCF8574& operator=(PCF8574&&) = delete;         // Move assignment operator deleted

private:
  static constexpr uint32_t clockSpeed = 100000U; // Default I2C clock speed
  const uint8_t address;
  TwoWire &wire;
  uint8_t registerValue;
};

#endif // PCF8574_HPP
