#pragma once

#include <Arduino.h>                                                /// F() macro for the flash-resident keys.
#include <string.h>                                                 /// String comparison utilities.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "mqttTopics.hpp"                                           /// The subtopic a message may be routed onward from.

/// @brief Where one arriving MQTT message should be delivered.
/// @details A message is normally handled by the module its subtopic names. An envelope on the
/// device-wide subtopic reaches any module, for a sender that can only publish to that one.
/// Kept out of the router so the host tests can reach the decision.
namespace MessageRoute {
  /// @brief Result of reading a message's envelope.
  struct Route {
    const char* target = nullptr;             // Subtopic the message is for; null when the envelope is unusable.
    JsonVariant body;                         // What the handler should be shown.
    bool rerouted = false;                    // Whether an envelope sent this somewhere other than where it arrived.
  };

  /// @brief Reads the optional envelope around a message.
  /// @details `{"to":"<subtopic>","msg":{...}}` hands the inner object to that subtopic's
  /// handler; anything else is handed on as it arrived. Read once and never read again, so a
  /// message cannot circle. Refused when it arrived anywhere but the device-wide subtopic, or
  /// when it names the subtopic it arrived on.
  /// @param arrivedOn Subtopic the message was published to.
  /// @param message The parsed payload.
  /// @return Where to deliver it, or a null target when the envelope cannot be honoured.
  [[nodiscard]] inline Route resolve(const char* arrivedOn, JsonVariant message) {
    Route route;
    if(arrivedOn == nullptr) { return route; }

    JsonVariant toJsonVar = message[F("to")];
    if(!toJsonVar.is<const char*>()) {
      route.target = arrivedOn;
      route.body = message;
      return route;
    }

    if(strcmp(arrivedOn, MqttTopics::getCommonSubtopic()) != 0) { return route; }

    const char* target = toJsonVar.as<const char*>();
    JsonVariant body = message[F("msg")];
    if((target == nullptr) || (target[0] == '\0')) { return route; }
    if(!body.is<JsonObject>()) { return route; }
    if(strcmp(target, arrivedOn) == 0) { return route; }

    route.target = target;
    route.body = body;
    route.rerouted = true;
    return route;
  }
} // namespace MessageRoute
