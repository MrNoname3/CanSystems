#pragma once
#include <Arduino.h>                                                /// Arduino libraries header.
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief 
class RF433Sender final {
private:
  // Protocol 1 constants (Princeton, PT-2240)
  static constexpr uint16_t pulseLength = 350;  // Base pulse length in microseconds
  static constexpr uint8_t headerHigh = 1;      // Header pulse: 1 high, 31 low
  static constexpr uint8_t headerLow = 31;
  static constexpr uint8_t zeroHigh = 1;        // Zero bit: 1 high, 3 low
  static constexpr uint8_t zeroLow = 3;
  static constexpr uint8_t oneHigh = 3;         // One bit: 3 high, 1 low
  static constexpr uint8_t oneLow = 1;

  // Pre-calculated timing constants for better performance
  static constexpr uint16_t headerHighTime = pulseLength * headerHigh;
  static constexpr uint16_t headerLowTime = pulseLength * headerLow;
  static constexpr uint16_t zeroHighTime = pulseLength * zeroHigh;
  static constexpr uint16_t zeroLowTime = pulseLength * zeroLow;
  static constexpr uint16_t oneHighTime = pulseLength * oneHigh;
  static constexpr uint16_t oneLowTime = pulseLength * oneLow;

public:
  /// @brief 
  /// @param pin 
  explicit RF433Sender(uint8_t pin) : transmitterPin(pin) {
    pinMode(transmitterPin, OUTPUT);
    digitalWrite(transmitterPin, LOW);
  }

  /// @brief Destructor of the object.
  ~RF433Sender() = default;

  /// @brief 
  /// @param code 
  /// @param bitLength 
  /// @param repeatCount 
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

  // Static convenience function for one-time transmissions
  // Trade-off: Higher runtime overhead, but saves memory when not used frequently

  /// @brief 
  /// @param pin 
  /// @param code 
  /// @param bitLength 
  /// @param repeatCount 
  static void transmit(uint8_t pin, const uint8_t* __restrict__ code, uint8_t bitLength, uint8_t repeatCount = 5) {
    RF433Sender sender(pin);
    sender.send(code, bitLength, repeatCount);
  }

  // Disable copy and move operations
  RF433Sender(const RF433Sender&) = delete;
  RF433Sender& operator=(const RF433Sender&) = delete;
  RF433Sender(RF433Sender&&) = delete;
  RF433Sender& operator=(RF433Sender&&) = delete;

private:
  const uint8_t transmitterPin;                 // 
};
