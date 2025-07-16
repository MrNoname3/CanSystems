#pragma once
#include <Arduino.h>                                                /// Arduino libraries header.
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief A high-performance 433MHz RF transmitter class implementing Protocol 1 (Princeton, PT-2240).
/// This class provides optimized transmission of binary data over 433MHz RF modules using
/// pulse-width modulation with pre-calculated timing constants for better performance.
class RF433Sender final {
private:
  // Protocol 1 constants (Princeton, PT-2240)
  static constexpr uint16_t pulseLength = 350;  // Base pulse length in microseconds
  static constexpr uint8_t headerHigh = 1;      // Header pulse: 1 high, 31 low
  static constexpr uint8_t headerLow = 31;      // Header pulse low duration multiplier
  static constexpr uint8_t zeroHigh = 1;        // Zero bit: 1 high, 3 low
  static constexpr uint8_t zeroLow = 3;         // Zero bit low duration multiplier
  static constexpr uint8_t oneHigh = 3;         // One bit: 3 high, 1 low
  static constexpr uint8_t oneLow = 1;          // One bit low duration multiplier

  // Pre-calculated timing constants for better performance
  static constexpr uint16_t headerHighTime = pulseLength * headerHigh;  // Pre-calculated header high time
  static constexpr uint16_t headerLowTime = pulseLength * headerLow;    // Pre-calculated header low time
  static constexpr uint16_t zeroHighTime = pulseLength * zeroHigh;      // Pre-calculated zero bit high time
  static constexpr uint16_t zeroLowTime = pulseLength * zeroLow;        // Pre-calculated zero bit low time
  static constexpr uint16_t oneHighTime = pulseLength * oneHigh;        // Pre-calculated one bit high time
  static constexpr uint16_t oneLowTime = pulseLength * oneLow;          // Pre-calculated one bit low time

public:
  /// @brief Constructs an RF433Sender object and initializes the transmitter pin.
  /// @param pin The digital pin number connected to the RF transmitter module data input
  explicit RF433Sender(uint8_t pin) : transmitterPin(pin) {
    pinMode(transmitterPin, OUTPUT);
    digitalWrite(transmitterPin, LOW);
  }

  /// @brief Destructor of the object.
  ~RF433Sender() = default;

  /// @brief Transmits binary data over 433MHz RF using Protocol 1 encoding.
  /// @param code Pointer to byte array containing the data to transmit (MSB first)
  /// @param bitLength Number of bits to transmit from the data array
  /// @param repeatCount Number of times to repeat the transmission (default: 5)
  void send(const uint8_t* __restrict__ code, uint8_t bitLength, uint8_t repeatCount = 5) {
    // Repeat transmission
    for (uint8_t repeat = 0; repeat < repeatCount; ++repeat) {
      // Send header (sync pulse) - inlined for performance
      digitalWrite(transmitterPin, HIGH);
      delayMicroseconds(headerHighTime);
      digitalWrite(transmitterPin, LOW);
      delayMicroseconds(headerLowTime);

      // Send data bits from MSB to LSB
      for (int8_t i = bitLength - 1; i >= 0; --i) {
        const uint8_t byteIndex = (bitLength - 1 - i) >> 3;  // Which byte (corrected)
        const uint8_t bitIndex = i & 7;    // Which bit within byte (MSB first)
        const uint8_t bitMask = 1 << bitIndex;

        if (code[byteIndex] & bitMask) {
          // Send '1' bit - inlined
          digitalWrite(transmitterPin, HIGH);
          delayMicroseconds(oneHighTime);
          digitalWrite(transmitterPin, LOW);
          delayMicroseconds(oneLowTime);
        } else {
          // Send '0' bit - inlined
          digitalWrite(transmitterPin, HIGH);
          delayMicroseconds(zeroHighTime);
          digitalWrite(transmitterPin, LOW);
          delayMicroseconds(zeroLowTime);
        }
      }
    }

    // Ensure line is low after transmission
    digitalWrite(transmitterPin, LOW);
  }

  /// @brief Static convenience function for one-time transmissions without creating a persistent object.
  /// @param pin The digital pin number connected to the RF transmitter module data input
  /// @param code Pointer to byte array containing the data to transmit (MSB first)
  /// @param bitLength Number of bits to transmit from the data array
  /// @param repeatCount Number of times to repeat the transmission (default: 5)
  /// @note This function has higher runtime overhead but saves memory when not used frequently
  static void transmit(uint8_t pin, const uint8_t* __restrict__ code, uint8_t bitLength, uint8_t repeatCount = 5) {
    RF433Sender sender(pin);
    sender.send(code, bitLength, repeatCount);
  }

  // Disable copy and move operations
  RF433Sender(const RF433Sender&) = delete;            // Deleted copy constructor
  RF433Sender& operator=(const RF433Sender&) = delete; // Deleted copy assignment operator
  RF433Sender(RF433Sender&&) = delete;                 // Deleted move constructor
  RF433Sender& operator=(RF433Sender&&) = delete;      // Deleted move assignment operator

private:
  const uint8_t transmitterPin;                 // Digital pin number connected to RF transmitter module
};