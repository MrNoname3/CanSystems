#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Intrusive list of the CAN devices a handler routes received frames to.
/// @details The link lives in the device itself, so registering one costs no allocation - which
/// is what an embedded target wants. Devices register from their own constructor and are never
/// removed, so the list only ever grows, and appending is O(1) through the kept tail.
///
/// Kept free of the CAN handler and the FreeRTOS plumbing so it can be unit-tested on the host,
/// the same way CanFramePump and OtaCanResponse are.
///
/// @tparam Device Node type providing `getClientCanId()`, `getNextDevice()` and `setNextDevice()`.
template<typename Device>
class CanDeviceList final {
public:
  /// @brief Registers a device at the end of the list.
  /// @param device Device to register.
  /// @return `false` when `device` is null; `true` once it is linked in.
  [[nodiscard]] bool append(Device* device) {
    if(device == nullptr) { return false; }
    if(tail != nullptr) {
      tail->setNextDevice(device);
    } else {
      head = device;
    }
    tail = device;
    return true;
  }

  /// @brief Finds the device registered for a client id.
  /// @param clientCanId Client CAN id taken from a received frame's sender field.
  /// @return The first device registered with that id, or `nullptr` when no device claims it.
  [[nodiscard]] Device* find(uint16_t clientCanId) const {
    for(Device* device = head; device != nullptr; device = device->getNextDevice()) {
      if(device->getClientCanId() == clientCanId) { return device; }
    }
    return nullptr;
  }

  /// @brief First registered device, for walking the whole list.
  /// @return The head of the list, or `nullptr` when nothing is registered.
  [[nodiscard]] Device* first() const { return head; }

private:
  Device* head = nullptr;                                           // First registered device.
  Device* tail = nullptr;                                           // Last registered device, kept for O(1) append.
};
