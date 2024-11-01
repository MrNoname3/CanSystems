#ifndef MULTIPLEXER_HPP
#define MULTIPLEXER_HPP

#include <stdint.h>

class Multiplexer final {
private:
  static constexpr uint8_t numSelectPins = 4U;  // Fixed number of select pins.
public:
  Multiplexer(uint8_t readPin, uint8_t enablePin, const uint8_t (&chSelectPins)[numSelectPins]);
  ~Multiplexer() = default;

  // Enables the multiplexer for reading.
  void enableRead() const;
  // Disables the multiplexer after reading.
  void disableRead() const;
  // Reads the selected channel and enables/disables during the read.
  uint16_t analogReadSimple(uint8_t channel) const;
  // Reads the selected channel without toggling enable pin.
  uint16_t analogReadAdvanced(uint8_t channel) const;

  Multiplexer(const Multiplexer&) = delete;
  Multiplexer& operator=(const Multiplexer&) = delete;
  Multiplexer(Multiplexer&&) = delete;
  Multiplexer& operator=(Multiplexer&&) = delete;

private:
  // Sets the channel by configuring select pins based on 4-bit channel value.
  void selectChannel(uint8_t channel) const;

  const uint8_t readPin;
  const uint8_t enablePin;
  const uint8_t (&chSelectPins)[numSelectPins];
};
#endif // MULTIPLEXER_HPP
