#ifndef PCF8574_HPP
#define PCF8574_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <Wire.h>                                                   /// I2C driver library.
#include "taskHandler.hpp"                                          /// Class for task scheduling.

/// @brief Class to control a PCF8574 I2C I/O expander.
/// @details Provides methods to read from and write to the pins of the PCF8574
/// via I2C communication. Supports pin toggling, input/output configuration, and state reading.
class PCF8574 final : public Task {
public:
  /// @brief Constructs a PCF8574 instance with the specified I2C address and bus.
  /// @param timeoutMs Timeout in microseconds for I2C communication.
  /// @param address The I2C address of the PCF8574 device.
  /// @param wire The `TwoWire` instance representing the I2C bus (defaults to `Wire`).
  PCF8574(uint32_t timeoutUs, uint8_t address, TwoWire &wire = Wire);

  /// @brief Destructor of the PCF8574 object.
  ~PCF8574() = default;

  /// @brief Initializes the PCF8574 device.
  /// @return `true` if the device is successfully initialized, `false` otherwise.
  virtual bool init() override;

  /// @brief Provides an empty implementation of the method.
  virtual void run() override {}

  /// @brief Writes a byte to the PCF8574 register.
  /// @param reg The byte to write to the PCF8574.
  /// @return `true` if the write operation was successful, `false` otherwise.
  bool write(uint8_t reg);

  /// @brief Reads a byte from the PCF8574 register.
  /// @param value Reference to a byte variable to store the read value.
  /// @return `true` if the read operation was successful, `false` otherwise.
  bool read(uint8_t &value) const;

  /// @brief Retrieves the current value of the PCF8574 register.
  /// @return The last known value written to or read from the PCF8574 register.
  const uint8_t getRegisterValue() const;

  /// @brief Configures a specific pin as an input.
  /// @param pin The pin number (0-7) to set as an input.
  /// @return `true` if the operation was successful, `false` otherwise.
  bool setAsInput(uint8_t pin);

  /// @brief Sets the state of a specific pin.
  /// @param pin The pin number (0-7) to configure.
  /// @param pinState The desired state of the pin: `1` for HIGH, `0` for LOW.
  /// @return `true` if the operation was successful, `false` otherwise.
  bool digitalWrite(uint8_t pin, uint8_t pinState);

  /// @brief Reads the state of a specific pin.
  /// @param pin The pin number (0-7) to read.
  /// @return The state of the pin: `1` for HIGH, `0` for LOW, or `-1` if the operation failed.
  uint8_t digitalRead(uint8_t pin) const;

  /// @brief Toggles the state of a specific pin.
  /// @param pin The pin number (0-7) to toggle.
  /// @return `true` if the operation was successful, `false` otherwise.
  bool toggleState(uint8_t pin);

  PCF8574(const PCF8574&) = delete;               // Copy constructor deleted
  PCF8574& operator=(const PCF8574&) = delete;    // Copy assignment operator deleted
  PCF8574(PCF8574&&) = delete;                    // Move constructor deleted
  PCF8574& operator=(PCF8574&&) = delete;         // Move assignment operator deleted

private:
  static constexpr uint32_t clockSpeed = 100000U; // Default I2C clock speed in Hz.

  const uint8_t address;                          // I2C address of the PCF8574 device.
  TwoWire &wire;                                  // Reference to the I2C bus used for communication.
  uint8_t registerValue;                          // Cached register value to track the current pin states.
  bool deviceExists;                              // Indicates if the device is present on the I2C bus.
};
#endif // PCF8574_HPP
