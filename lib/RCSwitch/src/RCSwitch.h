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

#include "Arduino.h"
#include <stdint.h>

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

  void send(uint64_t code, uint32_t length);

#if not defined(RCSwitchDisableReceiving)
  void enableReceive(int32_t interrupt);
  void enableReceive();
  void resumeReceive(int32_t interrupt);
  void disableReceive();
  bool available();
  void resetAvailable();

  uint64_t getReceivedValue();
  uint32_t getReceivedBitlength();
  uint32_t getReceivedDelay();
  uint32_t getReceivedProtocol();
  uint8_t getNumProtos();
#endif

  void enableTransmit(int32_t nTransmitterPin);
  void setPulseLength(int32_t nPulseLength);
  void setRepeatTransmit(int32_t nRepeatTransmit);
#if not defined(RCSwitchDisableReceiving)
#endif

  /**
   * Description of a single pule, which consists of a high signal
   * whose duration is "high" times the base pulse length, followed
   * by a low signal lasting "low" times the base pulse length.
   * Thus, the pulse overall lasts (high+low)*pulseLength
   */
  struct HighLow {
    uint8_t high;
    uint8_t low;
  };

  /**
   * A "protocol" describes how zero and one bits are encoded into high/low
   * pulses.
   */
  struct Protocol {
    /** base pulse length in microseconds, e.g. 350 */
    uint16_t pulseLength;
    uint8_t PreambleFactor;
    HighLow Preamble;
    uint8_t HeaderFactor;
    HighLow Header;

    HighLow zero;
    HighLow one;

    /**
     * If true, interchange high and low logic levels in all transmissions.
     *
     * By default, RCSwitch assumes that any signals it sends or receives
     * can be broken down into pulses which start with a high signal level,
     * followed by a a low signal level. This is e.g. the case for the
     * popular PT 2260 encoder chip, and thus many switches out there.
     *
     * But some devices do it the other way around, and start with a low
     * signal level, followed by a high signal level, e.g. the HT6P20B. To
     * accommodate this, one can set invertedSignal to true, which causes
     * RCSwitch to change how it interprets any HighLow struct FOO: It will
     * then assume transmissions start with a low signal lasting
     * FOO.high*pulseLength microseconds, followed by a high signal lasting
     * FOO.low*pulseLength microseconds.
     */
    bool invertedSignal;
    uint16_t Guard;
  };

  void setProtocol(Protocol protocol);
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
