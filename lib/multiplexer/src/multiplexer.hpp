#ifndef MULTIPLEXER_HPP
#define MULTIPLEXER_HPP

#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Class to manage a 16-channel analog multiplexer.
/// @details Provides functionality to select channels, enable/disable the multiplexer,
/// and read analog signals from the selected channel.
class Multiplexer final {
private:
  static constexpr uint8_t numSelectPins = 4U;  // Fixed number of select pins.

public:
  /// @brief Constructs the Multiplexer object.
  /// @param readPin Analog pin to read the multiplexer output.
  /// @param enablePin Digital pin to enable or disable the multiplexer.
  /// @param chSelectPins Array of digital pins used for channel selection (4 pins required).
  Multiplexer(uint8_t readPin, uint8_t enablePin, const uint8_t (&chSelectPins)[numSelectPins]);

  /// @brief Default destructor.
  ~Multiplexer() = default;

  /// @brief Selects a specific channel on the multiplexer.
  /// @param channel Channel number to select (0-15). Only the lower 4 bits are used.
  void selectChannel(uint8_t channel) const;

  /// @brief Enables the multiplexer, allowing signals to be read from the selected channel.
  void enableRead() const;

  /// @brief Disables the multiplexer, isolating the output.
  void disableRead() const;

  /// @brief Reads the analog value from the specified channel with automatic enabling/disabling.
  /// @param channel Channel number to read (0-15). Only the lower 4 bits are used.
  /// @return 10-bit analog value from the specified channel.
  [[nodiscard]] uint16_t analogReadSimple(uint8_t channel) const;

  /// @brief Reads the analog value from the specified channel without toggling the enable pin.
  /// @param channel Channel number to read (0-15). Only the lower 4 bits are used.
  /// @return 10-bit analog value from the specified channel.
  [[nodiscard]] uint16_t analogReadAdvanced(uint8_t channel) const;

  /// @brief Reads the analog value from the currently selected channel without toggling the enable pin.
  /// @return 10-bit analog value from the currently selected channel.
  [[nodiscard]] uint16_t analogReadAdvanced() const;

  Multiplexer(const Multiplexer&) = delete;               // Define copy constructor.
  Multiplexer& operator=(const Multiplexer&) = delete;    // Define copy assignment operator.
  Multiplexer(Multiplexer&&) = delete;                    // Define move constructor.
  Multiplexer& operator=(Multiplexer&&) = delete;         // Define move assignment operator.

private:
  const uint8_t readPin;                                  // Analog pin to read the output of the multiplexer.
  const uint8_t enablePin;                                // Digital pin to enable/disable the multiplexer.
  const uint8_t (&chSelectPins)[numSelectPins];           // Array of pins used for channel selection.
};
#endif // MULTIPLEXER_HPP
