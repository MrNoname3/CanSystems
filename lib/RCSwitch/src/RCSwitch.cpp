/*
  RCSwitch - Arduino libary for remote control outlet switches
  Copyright (c) 2011 Suat Özgür.  All right reserved.

  Contributors:
  - Andre Koehler / info(at)tomate-online(dot)de
  - Gordeev Andrey Vladimirovich / gordeev(at)openpyro(dot)com
  - Skineffect / http://forum.ardumote.com/viewtopic.php?f=2&t=46
  - Dominik Fischer / dom_fischer(at)web(dot)de
  - Frank Oltmanns / <first name>.<last name>(at)gmail(dot)com
  - Andreas Steinel / A.<lastname>(at)gmail(dot)com
  - Max Horn / max(at)quendi(dot)de
  - Robert ter Vehn / <first name>.<last name>(at)gmail(dot)com
  - Johann Richard / <first name>.<last name>(at)gmail(dot)com
  - Vlad Gheorghe / <first name>.<last name>(at)gmail(dot)com https://github.com/vgheo

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

#include "RCSwitch.h"

#include <algorithm>
#include <limits>

/* Protocol description format
 *
 * {
 *    Pulse length,
 *
 *    PreambleFactor,
 *    Preamble {high,low},
 *
 *    HeaderFactor,
 *    Header {high,low},
 *
 *    "0" bit {high,low},
 *    "1" bit {high,low},
 *
 *    Inverted Signal,
 *    Guard time
 * }
 *
 * Pulse length: pulse duration (Te) in microseconds,
 *               for example 350
 * PreambleFactor: Number of high and low states to send
 *                 (One pulse = 2 states, in orther words, number of pulses is
 *                 ceil(PreambleFactor/2).)
 * Preamble: Pulse shape which defines a preamble bit.
 *           Sent ceil(PreambleFactor/2) times.
 *           For example, {1, 2} with factor 3 would send
 *      _    _
 *     | |__| |__         (each horizontal bar has a duration of Te,
 *                         vertical bars are ignored)
 * HeaderFactor: Number of times to send the header pulse.
 * Header: Pulse shape which defines a header (or "sync"/"clock") pulse.
 *           {1, 31} means one pulse of duration 1 Te high and 31 Te low
 *      _
 *     | |_______________________________ (don't count the vertical bars)
 *
 * "0" bit: pulse shape defining a data bit, which is a logical "0"
 *          {1, 3} means 1 pulse duration Te high level and 3 low
 *      _
 *     | |___
 *
 * "1" bit: pulse shape that defines the data bit, which is a logical "1"
 *          {3, 1} means 3 pulses with a duration of Te high level and 1 low
 *      ___
 *     |   |_
 *
 * (note: to form the state bit Z (Tri-State bit), two codes are combined)
 *
 * Inverted Signal: Signal inversion - if true the signal is inverted
 *                  replacing high to low in a transmitted / received packet
 * Guard time: Separation time between two retries. It will be followed by the
 *             next preamble of the next packet. In number of Te.
 *             e.g. 39 pulses of duration Te low level
 */

#if defined(ESP8266) || defined(ESP32)
static constexpr RCSwitch::Protocol proto[] = {
#else
static const RCSwitch::Protocol PROGMEM proto[] = {
#endif
  { 350, 0, { 0, 0 }, 1, { 1, 31 }, { 1, 3 }, { 3, 1 }, false, 0 },  // 01 (Princeton, PT-2240)
};

enum {
  numProto = sizeof(proto) / sizeof(proto[0])
};

#if not defined(RCSwitchDisableReceiving)
volatile uint64_t RCSwitch::nReceivedValue = 0;
volatile uint64_t RCSwitch::nReceiveProtocolMask;
volatile uint32_t RCSwitch::nReceivedBitlength = 0;
volatile uint32_t RCSwitch::nReceivedDelay = 0;
volatile uint32_t RCSwitch::nReceivedProtocol = 0;
int32_t RCSwitch::nReceiveTolerance = 60;
uint32_t RCSwitch::nSeparationLimit = rcSwitchSeparationLimit;
uint32_t RCSwitch::timings[rcSwitchMaxChanges];
uint32_t RCSwitch::buftimings[4];
#endif

RCSwitch::RCSwitch() {
  this->nTransmitterPin = -1;
  this->setRepeatTransmit(5);
  this->setProtocol(1);
#if not defined(RCSwitchDisableReceiving)
  this->nReceiverInterrupt = -1;
  RCSwitch::nReceiveTolerance = 60;
  RCSwitch::nReceivedValue = 0;
  RCSwitch::nReceiveProtocolMask = (1ULL << numProto) - 1ULL;  // pow(2,numProto)-1;
#endif
}

uint8_t RCSwitch::getNumProtos() { // NOLINT(readability-convert-member-functions-to-static)
  return numProto;
}

/**
 * Sets the protocol to send.
 */
void RCSwitch::setProtocol(Protocol protocol) {
  this->protocol = protocol;
}

/**
 * Sets the protocol to send, from a list of predefined protocols
 */
void RCSwitch::setProtocol(int32_t nProtocol) { // NOLINT(readability-convert-member-functions-to-static)
  if(nProtocol < 1 || nProtocol > numProto) {
    nProtocol = 1;  // Out of range: fall back to the first protocol.
  }
#if defined(ESP8266) || defined(ESP32)
  this->protocol = proto[nProtocol - 1];
#else
  memcpy_P(&this->protocol, &proto[nProtocol - 1], sizeof(Protocol));
#endif
}

/**
 * Sets pulse length in microseconds
 */
void RCSwitch::setPulseLength(int32_t nPulseLength) {
  this->protocol.pulseLength = nPulseLength;
}

/**
 * Sets Repeat Transmits
 */
void RCSwitch::setRepeatTransmit(int32_t nRepeatTransmit) {
  this->nRepeatTransmit = nRepeatTransmit;
}

/**
 * Enable transmissions
 *
 * @param nTransmitterPin    Arduino Pin to which the sender is connected to
 */
void RCSwitch::enableTransmit(int32_t nTransmitterPin) {
  this->nTransmitterPin = nTransmitterPin;
  pinMode(this->nTransmitterPin, OUTPUT);
}

/**
 * @param duration   no. of microseconds to delay
 */
static inline void safeDelayMicroseconds(uint32_t duration) {
#if defined(ESP8266) || defined(ESP32)
  if(duration > 10000) {
    // if delay > 10 milliseconds, use yield() to avoid wdt reset
    uint32_t start = micros();
    while((micros() - start) < duration) { // NOLINT(bugprone-infinite-loop)
      yield();
    }
  } else {
    delayMicroseconds(duration);
  }
#else
  delayMicroseconds(duration);
#endif
}

/**
 * Transmit the first 'length' bits of the integer 'code'. The
 * bits are sent from MSB to LSB, i.e., first the bit at position length-1,
 * then the bit at position length-2, and so on, till finally the bit at position 0.
 */
void RCSwitch::send(uint64_t code, uint32_t length) {
  if(this->nTransmitterPin == -1) { return; }

#if not defined(RCSwitchDisableReceiving)
  // make sure the receiver is disabled while we transmit
  int32_t nReceiverInterrupt_backup = nReceiverInterrupt;
  if(nReceiverInterrupt_backup != -1) {
    this->disableReceive();
  }
#endif

  // repeat sending the packet nRepeatTransmit times
  for(int32_t nRepeat = 0; nRepeat < nRepeatTransmit; nRepeat++) {
    // send the preamble
    for(int32_t i = 0; i < ((protocol.PreambleFactor / 2) + (protocol.PreambleFactor % 2)); i++) {
      this->transmit({ protocol.Preamble.high, protocol.Preamble.low });
    }
    // send the header
    if(protocol.HeaderFactor > 0) {
      for(int32_t i = 0; i < protocol.HeaderFactor; i++) {
        this->transmit(protocol.Header);
      }
    }
    // send the code
    for(int32_t i = static_cast<int32_t>(length) - 1; i >= 0; i--) {
      if((code & (1ULL << i)) != 0ULL) {
        this->transmit(protocol.one);
      } else {
        this->transmit(protocol.zero);
      }
    }
    // Set the guard Time
    if(protocol.Guard > 0) {
      digitalWrite(this->nTransmitterPin, LOW);
      safeDelayMicroseconds(static_cast<uint32_t>(this->protocol.pulseLength) * protocol.Guard);
    }
  }

  // Disable transmit after sending (i.e., for inverted protocols)
  digitalWrite(this->nTransmitterPin, LOW);

#if not defined(RCSwitchDisableReceiving)
  // enable receiver again if we just disabled it, keeping any frame that arrived beforehand
  if(nReceiverInterrupt_backup != -1) {
    this->resumeReceive(nReceiverInterrupt_backup);
  }
#endif
}

/**
 * Transmit a single high-low pulse.
 */
void RCSwitch::transmit(HighLow pulses) const {
  uint8_t firstLogicLevel = (this->protocol.invertedSignal) ? LOW : HIGH;
  uint8_t secondLogicLevel = (this->protocol.invertedSignal) ? HIGH : LOW;

  if(pulses.high > 0) {
    digitalWrite(this->nTransmitterPin, firstLogicLevel);
    safeDelayMicroseconds(static_cast<uint32_t>(this->protocol.pulseLength) * pulses.high);
  }
  if(pulses.low > 0) {
    digitalWrite(this->nTransmitterPin, secondLogicLevel);
    safeDelayMicroseconds(static_cast<uint32_t>(this->protocol.pulseLength) * pulses.low);
  }
}

#if not defined(RCSwitchDisableReceiving)
/**
 * Enable receiving data
 */
void RCSwitch::enableReceive(int32_t interrupt) {
  this->nReceiverInterrupt = interrupt;
  this->enableReceive();
}

void RCSwitch::enableReceive() { // NOLINT(readability-make-member-function-const)
  if(this->nReceiverInterrupt != -1) {
    // Starting reception discards whatever the previous session left behind.
    RCSwitch::nReceivedValue = 0;
    RCSwitch::nReceivedBitlength = 0;
    this->attachReceiveInterrupt();
  }
}

void RCSwitch::resumeReceive(int32_t interrupt) { // NOLINT(readability-make-member-function-const)
  this->nReceiverInterrupt = interrupt;
  if(this->nReceiverInterrupt != -1) {
    this->attachReceiveInterrupt();
  }
}

void RCSwitch::attachReceiveInterrupt() const {
  attachInterrupt(this->nReceiverInterrupt, handleInterrupt, CHANGE);
}

/**
 * Disable receiving data
 */
void RCSwitch::disableReceive() {
  detachInterrupt(this->nReceiverInterrupt);
  this->nReceiverInterrupt = -1;
}

bool RCSwitch::available() { // NOLINT(readability-convert-member-functions-to-static)
  return RCSwitch::nReceivedValue != 0;
}

void RCSwitch::resetAvailable() { // NOLINT(readability-convert-member-functions-to-static)
  RCSwitch::nReceivedValue = 0;
}

uint64_t RCSwitch::getReceivedValue() { // NOLINT(readability-convert-member-functions-to-static)
  return RCSwitch::nReceivedValue;
}

uint32_t RCSwitch::getReceivedBitlength() { // NOLINT(readability-convert-member-functions-to-static)
  return RCSwitch::nReceivedBitlength;
}

uint32_t RCSwitch::getReceivedDelay() { // NOLINT(readability-convert-member-functions-to-static)
  return RCSwitch::nReceivedDelay;
}

uint32_t RCSwitch::getReceivedProtocol() { // NOLINT(readability-convert-member-functions-to-static)
  return RCSwitch::nReceivedProtocol;
}

/* helper function for the receiveProtocol method */
uint32_t RCSwitch::diff(int32_t A, int32_t B) {
  return abs(A - B);
}

bool RCSwitch::receiveProtocol(const int32_t p, uint32_t changeCount) { // NOLINT(readability-convert-member-functions-to-static)
#if defined(ESP8266) || defined(ESP32)
  const Protocol& pro = proto[p - 1];
#else
  Protocol pro;
  memcpy_P(&pro, &proto[p - 1], sizeof(Protocol));
#endif

  uint64_t code = 0;
  uint32_t FirstTiming = 0;
  if(pro.PreambleFactor > 0) {
    FirstTiming = pro.PreambleFactor + 1;
  }
  uint32_t BeginData = 0;
  if(pro.HeaderFactor > 0) {
    BeginData = (pro.invertedSignal) ? (2) : (1);
    // Header pulse count correction for more than one
    if(pro.HeaderFactor > 1) {
      BeginData += (pro.HeaderFactor - 1) * 2;
    }
  }
  // Assuming the longer pulse length is the pulse captured in timings[FirstTiming]
  // Take the larger of the two Header values.
  const uint32_t syncLengthInPulses = ((pro.Header.low) > (pro.Header.high)) ? (pro.Header.low) : (pro.Header.high);
  // Te is the first header pulse divided by the number of pulses it contains,
  // or the preamble pulse divided by the number of Te it contains.
  uint32_t sdelay = 0;
  if(syncLengthInPulses > 0) {
    sdelay = RCSwitch::timings[FirstTiming] / syncLengthInPulses;
  } else if(pro.PreambleFactor > 0) {
    sdelay = RCSwitch::timings[FirstTiming - 2] / pro.PreambleFactor;
  }
  const uint32_t delay = sdelay;
  if(delay == 0) {
    return false;
  }
  // nReceiveTolerance = 60
  // Pulse lengths may deviate by up to 60 %.
  const uint32_t delayTolerance = delay * RCSwitch::nReceiveTolerance / 100;

  // 0 - sync, ahead of the preamble or the data
  // BeginData - offset of 1 or 2 from sync to preamble/data
  // FirstTiming - offset from the preamble to the header
  // firstDataTiming - the first data pulse
  // bitChangeCount - number of pulses in the data

  /* For protocols that start low, the sync period looks like
   *               _________
   * _____________|         |XXXXXXXXXXXX|
   *
   * |--1st dur--|-2nd dur-|-Start data-|
   *
   * The 3rd saved duration starts the data.
   *
   * For protocols that start high, the sync period looks like
   *
   *  ______________
   * |              |____________|XXXXXXXXXXXXX|
   *
   * |-filtered out-|--1st dur--|--Start data--|
   *
   * The 2nd saved duration starts the data
   */
  // With invertedSignal false the signal starts at array element 1 (high level),
  // with invertedSignal true it starts at element 2 (low level).
  // Correct for the preamble and the header.
  const uint32_t firstDataTiming = BeginData + FirstTiming;
  uint32_t bitChangeCount = changeCount - firstDataTiming - 1U + static_cast<uint32_t>(pro.invertedSignal);
  if(bitChangeCount > 128U) {
    bitChangeCount = 128;
  }

  for(uint32_t i = firstDataTiming; i < firstDataTiming + bitChangeCount; i += 2) {
    code <<= 1;
    if(diff(static_cast<int32_t>(RCSwitch::timings[i]), static_cast<int32_t>(delay * pro.zero.high)) < delayTolerance &&
       diff(static_cast<int32_t>(RCSwitch::timings[i + 1]), static_cast<int32_t>(delay * pro.zero.low)) < delayTolerance) {
      // zero
    } else if(diff(static_cast<int32_t>(RCSwitch::timings[i]), static_cast<int32_t>(delay * pro.one.high)) < delayTolerance &&
              diff(static_cast<int32_t>(RCSwitch::timings[i + 1]), static_cast<int32_t>(delay * pro.one.low)) < delayTolerance) {
      // one
      code |= 1;
    } else {
      // Failed
      return false;
    }
  }

  if(bitChangeCount > 14) {    // ignore very short transmissions: no device sends them, so this must be noise
    RCSwitch::nReceivedValue = code;
    RCSwitch::nReceivedBitlength = bitChangeCount / 2;
    RCSwitch::nReceivedDelay = delay;
    RCSwitch::nReceivedProtocol = p;
    return true;
  }

  return false;
}

void RCSwitch::handleInterrupt() { // NOLINT(readability-convert-member-functions-to-static, readability-function-cognitive-complexity)

  static uint32_t changeCount = 0;
  static uint32_t lastTime = 0;
  static byte repeatCount = 0;

  const long time = micros();
  const uint32_t duration = time - lastTime;

  RCSwitch::buftimings[3] = RCSwitch::buftimings[2];
  RCSwitch::buftimings[2] = RCSwitch::buftimings[1];
  RCSwitch::buftimings[1] = RCSwitch::buftimings[0];
  RCSwitch::buftimings[0] = duration;

  if(duration > RCSwitch::nSeparationLimit ||
     changeCount == 156U ||
     (diff(static_cast<int32_t>(RCSwitch::buftimings[3]), static_cast<int32_t>(RCSwitch::buftimings[2])) < 50U &&
      diff(static_cast<int32_t>(RCSwitch::buftimings[2]), static_cast<int32_t>(RCSwitch::buftimings[1])) < 50U &&
      changeCount > 25U)) {
    // A pulse longer than nSeparationLimit (4300) arrived.
    // A long stretch without signal level change occurred. This could
    // be the gap between two transmission.
    if(diff(static_cast<int32_t>(duration), static_cast<int32_t>(RCSwitch::timings[0])) < 400U ||
       changeCount == 156U ||
       (diff(static_cast<int32_t>(RCSwitch::buftimings[3]), static_cast<int32_t>(RCSwitch::timings[1])) < 50U &&
        diff(static_cast<int32_t>(RCSwitch::buftimings[2]), static_cast<int32_t>(RCSwitch::timings[2])) < 50U &&
        diff(static_cast<int32_t>(RCSwitch::buftimings[1]), static_cast<int32_t>(RCSwitch::timings[3])) < 50U &&
        changeCount > 25U)) {
      // If its length differs from the first pulse received earlier by less than
      // +-200 (200 originally), this is a repeat of the same packet and is ignored.
      // This long signal is close in length to the long signal which
      // started the previously recorded timings; this suggests that
      // it may indeed by a a gap between two transmissions (we assume
      // here that a sender will send the signal multiple times,
      // with roughly the same gap between them).

      // Number of repeated packets.
      repeatCount++;
      // On the second repeat, start decoding the one received first.
      if(repeatCount == 1) {
        uint64_t thismask = 1;
        for(uint32_t i = 1; i <= numProto; i++) {
          if((RCSwitch::nReceiveProtocolMask & thismask) != 0ULL) {
            if(receiveProtocol(static_cast<int32_t>(i), changeCount)) {
              // receive succeeded for protocol i
              break;
            }
          }
          thismask <<= 1;
        }
        // Clear the repeat counter.
        repeatCount = 0;
      }
    }
    // The length differs by more than +-200 from the one received earlier:
    // clear the counter and start receiving a new packet.
    changeCount = 0;
    if(diff(static_cast<int32_t>(RCSwitch::buftimings[3]), static_cast<int32_t>(RCSwitch::buftimings[2])) < 50U &&
       diff(static_cast<int32_t>(RCSwitch::buftimings[2]), static_cast<int32_t>(RCSwitch::buftimings[1])) < 50U) {
      RCSwitch::timings[1] = RCSwitch::buftimings[3];
      RCSwitch::timings[2] = RCSwitch::buftimings[2];
      RCSwitch::timings[3] = RCSwitch::buftimings[1];
      changeCount = 4;
    }
  }

  // detect overflow
  if(changeCount >= rcSwitchMaxChanges) {
    changeCount = 0;
    repeatCount = 0;
  }

  // Store the length of the pulse just received.
  if(changeCount > 0 && duration < 100) { // ignore noise spikes shorter than 100 us
    RCSwitch::timings[changeCount - 1] += duration;
  } else {
    RCSwitch::timings[changeCount++] = duration;
  }
  lastTime = time;
}
#endif
