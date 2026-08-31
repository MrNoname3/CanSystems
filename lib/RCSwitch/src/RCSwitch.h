/*
  RCSwitch - Arduino libary for remote control outlet switches
  Copyright (c) 2011 Suat Özgür.  All right reserved.

  Contributors:
  - Andre Koehler / info(at)tomate-online(dot)de
  - Gordeev Andrey Vladimirovich / gordeev(at)openpyro(dot)com
  - Skineffect / http://forum.ardumote.com/viewtopic.php?f=2&t=46
  - Dominik Fischer / dom_fischer(at)web(dot)de
  - Frank Oltmanns / <first name>.<last name>(at)gmail(dot)com
  - Max Horn / max(at)quendi(dot)de
  - Robert ter Vehn / <first name>.<last name>(at)gmail(dot)com

  Project home: https://github.com/sui77/rc-switch/

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#ifndef RC_SWITCH_H
#define RC_SWITCH_H

#include "Arduino.h"                                                /// Arduino core types and pin functions.
#include <stdint.h>                                                 /// Standard fixed-width integer types.

#if defined(ESP8266)
// interrupt handler and related code must be in RAM on ESP8266,
// according to issue #46.
#define RECEIVE_ATTR IRAM_ATTR
#define VAR_ISR_ATTR
#elif defined(ESP32)
#define RECEIVE_ATTR IRAM_ATTR
#define VAR_ISR_ATTR DRAM_ATTR
#else
#define RECEIVE_ATTR
#define VAR_ISR_ATTR
#endif

// At least for the ATTiny X4/X5, receiving has to be disabled due to
// missing libm depencies (udivmodhi4)
#if defined(__AVR_ATtinyX5__) or defined(__AVR_ATtinyX4__)
#define RCSwitchDisableReceiving
#endif

// Number of maximum high/Low changes per packet.
// We can handle up to 36 bit * 2 H/L changes per bit + 2 for sync
// keeloq would need RCSWITCH_MAX_CHANGES raised to 23+1+66*2+1=157
// #define RCSWITCH_MAX_CHANGES 75        // default 75 - longest protocol that requires this buffer size is 38/nexus
#define RCSWITCH_MAX_CHANGES 131        // default 75 - Supports 64 too

// separationLimit: minimum microseconds between received codes, closer codes are ignored.
// according to discussion on issue #14 it might be more suitable to set the separation
// limit to the same time as the 'low' part of the sync signal for the current protocol.
// should be set to the minimum value of pulselength * the sync signal
#define RCSWITCH_SEPARATION_LIMIT 3600

class RCSwitch {
public:
  RCSwitch();
  ~RCSwitch() = default;

  RCSwitch(const RCSwitch&) = delete;                               // Define copy constructor.
  RCSwitch& operator=(const RCSwitch&) = delete;                    // Define copy assignment operator.
  RCSwitch(RCSwitch&&) = delete;                                    // Define move constructor.
  RCSwitch& operator=(RCSwitch&&) = delete;                         // Define move assignment operator.

  /// @brief Transmits one code, repeated as many times as setRepeatTransmit() asks for.
  /// @param code Code word to send.
  /// @param length Number of bits of `code` to send, counted from the least significant end.
  /// @note Blocks for the whole transmission - the pulses are bit-banged - and turns the
  /// receiver off for its duration, without discarding an already received frame.
  void send(uint64_t code, uint32_t length);

#if not defined(RCSwitchDisableReceiving)
  /// @brief Starts reception on an interrupt, discarding any frame held from before.
  /// @param interrupt Interrupt number the receiver pin is attached to.
  void enableReceive(int32_t interrupt);

  /// @brief Starts reception on the interrupt a previous enableReceive() selected.
  void enableReceive();

  /// @brief Re-attaches the receive interrupt, keeping a frame that arrived before it was
  /// detached.
  /// @param interrupt Interrupt number the receiver pin is attached to.
  /// @note Used after a transmission, which turns the receiver off and back on: a frame that
  /// arrived beforehand has not been read yet and is still the caller's to collect.
  void resumeReceive(int32_t interrupt);

  /// @brief Stops reception by detaching the interrupt.
  void disableReceive();

  /// @brief Reports whether a decoded frame is waiting to be collected.
  [[nodiscard]] bool available();

  /// @brief Discards the frame currently held, so the next one can be received.
  void resetAvailable();

  /// @brief Code word of the frame currently held, or 0 when there is none.
  [[nodiscard]] uint64_t getReceivedValue();

  /// @brief Number of bits the held frame carries.
  [[nodiscard]] uint32_t getReceivedBitlength();

  /// @brief Base pulse length of the held frame, in microseconds.
  [[nodiscard]] uint32_t getReceivedDelay();

  /// @brief Number of the protocol the held frame was decoded with, counted from 1.
  [[nodiscard]] uint32_t getReceivedProtocol();

  /// @brief Number of protocols the decoder knows, which is the highest valid protocol number.
  [[nodiscard]] uint8_t getNumProtos();
#endif

  /// @brief Selects the pin the transmitter is wired to and enables transmission.
  /// @param nTransmitterPin Digital output pin driving the transmitter.
  void enableTransmit(int32_t nTransmitterPin);

  /// @brief Overrides the base pulse length the current protocol brought with it.
  /// @param nPulseLength Pulse length in microseconds.
  void setPulseLength(int32_t nPulseLength);

  /// @brief Sets how many times send() repeats each code.
  /// @param nRepeatTransmit Repeat count; repeats are what make a lossy 433 MHz link arrive.
  void setRepeatTransmit(int32_t nRepeatTransmit);

  /// @brief One pulse: a high signal lasting `high` base pulse lengths, then a low signal
  /// lasting `low` of them, so the pulse takes (high + low) * pulseLength altogether.
  struct HighLow {
    uint8_t high;
    uint8_t low;
  };

  /// @brief How zero and one bits are encoded into high/low pulses.
  struct Protocol {
    uint16_t pulseLength;                             // Base pulse length in microseconds, e.g. 350.
    uint8_t PreambleFactor;
    HighLow Preamble;
    uint8_t HeaderFactor;
    HighLow Header;

    HighLow zero;
    HighLow one;

    /// @brief Swaps the high and low levels of every pulse.
    /// @details A pulse normally starts high and then goes low, as the widespread PT2260
    /// encoder does it. Some devices, the HT6P20B among them, start low instead; with this set
    /// a HighLow's `high` field times the pulse length becomes the low part and `low` the high
    /// part.
    bool invertedSignal;
    uint16_t Guard;
  };

  /// @brief Uses a protocol description of the caller's own.
  /// @param protocol Protocol to send and decode with.
  void setProtocol(Protocol protocol);

  /// @brief Selects one of the built-in protocols.
  /// @param nProtocol Protocol number, counted from 1; anything outside the table falls back
  /// to protocol 1.
  void setProtocol(int32_t nProtocol);

private:
  void transmit(HighLow pulses) const;
  void attachReceiveInterrupt() const;

#if not defined(RCSwitchDisableReceiving)
  inline static RECEIVE_ATTR void handleInterrupt() __attribute__((optimize("-O3")));
  inline static RECEIVE_ATTR bool receiveProtocol(int32_t p, uint32_t changeCount) __attribute__((optimize("-O3")));
  static inline uint32_t diff(int32_t A, int32_t B) __attribute__((optimize("-O3")));
  int32_t nReceiverInterrupt;
#endif
  int32_t nTransmitterPin;
  int32_t nRepeatTransmit;
  Protocol protocol;

#if not defined(RCSwitchDisableReceiving)
  static int32_t nReceiveTolerance;
  volatile static uint64_t nReceivedValue;
  volatile static uint64_t nReceiveProtocolMask;
  volatile static uint32_t nReceivedBitlength;
  volatile static uint32_t nReceivedDelay;
  volatile static uint32_t nReceivedProtocol;
  static uint32_t nSeparationLimit;
  /*
   * timings[0] contains sync timing, followed by a number of bits
   */
  static uint32_t timings[RCSWITCH_MAX_CHANGES];
  // Durations of the last four packets; [0] is the most recent.
  static uint32_t buftimings[4];
#endif
};
#endif // RC_SWITCH_H
