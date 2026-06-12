#pragma once
// Native-test shim for the CAN layer: a recording CanHandler plus a CanBase mirror, so the
// CAN-MQTT handler libraries (canMqttGateway, canAlertDriver) can be unit-tested on the host.
// canHandler.hpp includes this under NATIVE_TEST. CanHandlerBase (and CanFrame) are the real,
// platform-independent implementations; only the hardware send path is replaced by recording.
// CanBase mirrors the ESP32 CanBase API — the ESP build compiles the handlers against the real
// class, so any signature divergence here surfaces as a native-only compile error.
#include <stdint.h>
#include <vector>
#include "canHandlerBase.hpp"
#include "taskHandler.hpp"

class CanBase;                                                      // Forward declaration.

// Not final: specs with their own recording needs (pushButtonHandler, ambientSensor) subclass it.
class CanHandlerNative : public CanHandlerBase {
public:
  using CanHandlerBase::send;                                       // Keep the CanCmd convenience overloads visible.

  bool init() override { return true; }                             // NOLINT(readability-make-member-function-const)
  bool run() override { return true; }                              // NOLINT(readability-make-member-function-const)

  [[nodiscard]] bool send(uint16_t command, const uint8_t (&data)[8]) const override {
    return send(CanFrame(getMasterCanId(), command, getLocalCanId(), data));
  }

  // Mirrors CanHandlerEsp32::send(const CanFrame&) — including its non-[[nodiscard]] signature;
  // records instead of transmitting.
  bool send(const CanFrame& frameOut) const {                       // NOLINT(readability-convert-member-functions-to-static,modernize-use-nodiscard)
    if(!sendResult) { return false; }
    sentFrames.push_back(frameOut);
    return true;
  }

  bool registerCallback(CanBase* canBasePtr) {                      // NOLINT(readability-convert-member-functions-to-static)
    if(canBasePtr == nullptr) { return false; }
    registeredDevices.push_back(canBasePtr);
    return true;
  }

  // ---- test inspection (static so tests can read them without a handle) ----
  static inline std::vector<CanFrame> sentFrames;
  static inline std::vector<CanBase*> registeredDevices;
  static inline bool sendResult = true;
  static void resetState() {
    sentFrames.clear();
    registeredDevices.clear();
    sendResult = true;
  }
};
using CanHandler = CanHandlerNative;                                // Alias `CanHandler` like the hardware headers do.

/// Mirror of the ESP32 CanBase (canHandlerEsp32.hpp) without the FreeRTOS plumbing.
class CanBase : public virtual Task {
public:
  [[nodiscard]] bool init() override = 0;
  [[nodiscard]] bool run() override = 0;

  [[nodiscard]] inline bool isClientCanIdValid(uint16_t clientCanId) {
    const bool isLocalCanId = (clientCanId == canHandler.getLocalCanId());
    const bool isMasterCanId = (clientCanId == canHandler.getMasterCanId());
    return (!isLocalCanId && !isMasterCanId);
  }

  [[nodiscard]] inline uint16_t getClientCanId() const { return clientCanId; }

  virtual void canFrameArrivedCallback(const CanHandler::CanFrame& canFrame) = 0;

  [[nodiscard]] inline bool sendCanFrame(uint16_t command, const uint8_t (&data)[8]) const {
    return canHandler.send(CanHandler::CanFrame{getClientCanId(), command, canHandler.getLocalCanId(), data});
  }

  [[nodiscard]] inline bool sendCanFrame(CanCmd command, const uint8_t (&data)[8]) const {
    return sendCanFrame(static_cast<uint16_t>(command), data);
  }

  [[nodiscard]] inline bool sendCanFrame(uint16_t command) const {
    uint8_t data[8] = {0U};
    return sendCanFrame(command, data);
  }

  [[nodiscard]] inline bool sendCanFrame(CanCmd command) const {
    return sendCanFrame(static_cast<uint16_t>(command));
  }

  [[nodiscard]] bool sendCanResponse(uint16_t command, bool response) const {
    const uint8_t data[8] = {static_cast<uint8_t>(response), 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    return sendCanFrame(command, data);
  }

  [[nodiscard]] bool sendCanResponse(CanCmd command, bool response) const {
    return sendCanResponse(static_cast<uint16_t>(command), response);
  }

  CanBase(const CanBase&) = delete;
  CanBase& operator=(const CanBase&) = delete;
  CanBase(CanBase&&) = delete;
  CanBase& operator=(CanBase&&) = delete;

protected:
  CanBase(CanHandler& canHandler, uint16_t clientCanId) :
    canHandler(canHandler),
    clientCanId(clientCanId)
  {
    if(isClientCanIdValid(this->clientCanId)) {
      this->canHandler.registerCallback(this);
    }
  }

  ~CanBase() override = default;

private:
  CanHandler& canHandler;
  const uint16_t clientCanId;
};
