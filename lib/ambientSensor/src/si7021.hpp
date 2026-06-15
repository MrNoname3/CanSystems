#ifndef SI7021_HPP
#define SI7021_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <Wire.h>                                                   /// I2C driver library.

/// @brief Driver class for the SI7021 temperature and humidity sensor.
class SI7021 final {
public:
  /// @brief Precision configuration for temperature and humidity measurements.
  enum class Precision : uint8_t {
    T14RH12 = 0x00,                             // 14-bit temperature, 12-bit humidity (default).
    T12RH8 = 0x01,                              // 12-bit temperature, 8-bit humidity.
    T13RH10 = 0x80,                             // 13-bit temperature, 10-bit humidity.
    T11RH11 = 0x81                              // 11-bit temperature, 11-bit humidity.
  };

  /// @brief Constructs a SI7021 instance with the specified I2C address and bus.
  /// @param timeoutUs Timeout in microseconds for I2C communication.
  /// @param address The I2C address of the SI7021 device (default: `0x40`).
  /// @param wire The `TwoWire` instance representing the I2C bus (default: `Wire`).
  SI7021(uint32_t timeoutUs, uint8_t address = 0x40, TwoWire& wire = Wire);

  /// @brief Destructor of the SI7021 object.
  ~SI7021() = default;

  /// @brief Initializes the SI7021 device.
  /// @return `true` if the device is successfully initialized, `false` otherwise.
  bool init();

  /// @brief Reads the temperature in hundredths of a degree Celsius.
  /// @param temperature Reference to store the temperature value.
  /// @return `true` if the temperature was successfully read, `false` otherwise.
  bool getCelsiusHundredths(int16_t& temperature);

  /// @brief Reads the relative humidity as a percentage.
  /// @param humidity Reference to store the humidity value.
  /// @return `true` if the humidity was successfully read, `false` otherwise.
  bool getHumidityPercent(uint16_t& humidity);

  /// @brief Configures the measurement precision of the SI7021 sensor.
  /// @param precision The desired precision settings.
  /// @return `true` if the precision was successfully configured, `false` otherwise.
  bool setPrecision(Precision precision);

  /// @brief Enables or disables the internal heater.
  /// @param on `true` to enable the heater, `false` to disable it.
  /// @return `true` if the heater state was successfully set, `false` otherwise.
  bool setHeater(bool on);

  SI7021(const SI7021&) = delete;               // Copy constructor deleted
  SI7021& operator=(const SI7021&) = delete;    // Copy assignment operator deleted
  SI7021(SI7021&&) = delete;                    // Move constructor deleted
  SI7021& operator=(SI7021&&) = delete;         // Move assignment operator deleted

private:
  /// @brief I2C command set for the SI7021 sensor.
  enum class SI7021Commands : uint8_t {
    // clang-format off
    RH_READ     = 0xE5,                         // Command to read relative humidity.
    TEMP_READ   = 0xE3,                         // Command to read temperature
    RESET       = 0xFE,                         // Command to reset the sensor.
    USER1_READ  = 0xE7,                         // Command to read the user configuration register.
    USER1_WRITE = 0xE6                          // Command to write to the user configuration register.
    // clang-format on
  };

  /// @brief Writes data to the SI7021 over I2C.
  /// @param reg Pointer to the data to be written.
  /// @param regLen Length of the data array.
  /// @return `true` if the write operation was successful, `false` otherwise.
  bool writeReg(const uint8_t* reg, uint8_t regLen);

  /// @brief Reads data from the SI7021 over I2C.
  /// @param reg Pointer to the buffer for storing the read data.
  /// @param regLen Number of bytes to read.
  /// @return `true` if the read operation was successful, `false` otherwise.
  bool readReg(uint8_t* reg, uint8_t regLen);

  static constexpr uint32_t clockSpeed = 100000U; // Default I2C clock speed in Hz.

  const uint8_t address;                          // I2C address of the device.
  TwoWire& wire;                                  // Reference to the I2C bus used for communication.
  bool deviceExists;                              // Indicates if the device is present on the I2C bus.
};
#endif // SI7021_HPP
