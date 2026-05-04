#include "canAlertDriver.hpp"

CanAlertDriver::CanAlertDriver(CanHandler& canHandler, uint32_t canId,
  Connectivity& connectivity, const char* subTopic, float tempOffset) :
  CanMqttGateway::CanMqttGateway(canHandler, canId, connectivity, subTopic, FileName::getCanAlertFwLocation()),
  tempOffset(tempOffset)
{}

bool CanAlertDriver::publishDiscovery() {
  using HA = HADiscovery;
  buildCanTopics();

  const HA::CanDeviceConfig canDevConfig = {
    getCanDeviceId(),
    getCanDeviceName(),
    getCanSwVersion(),
    getCanAvailTopic(),
    MqttBase::getSubtopic(),
    canHwVersionStr
  };

  const HA::EntityConfig tempConfig = HA::EntityConfig::sensor(
    entityNameTemp, valTplTemp, unitDegC,
    HA::StateClass::measurement, HA::DeviceClass::temperature, iconTherm);
  bool result = doPublishCanDeviceEntityDiscovery(entitySubTemp, tempConfig, canDevConfig);

  const HA::EntityConfig humConfig = HA::EntityConfig::sensor(
    entityNameHum, valTplHum, unitPct,
    HA::StateClass::measurement, HA::DeviceClass::humidity, iconWater);
  result = doPublishCanDeviceEntityDiscovery(entitySubHum, humConfig, canDevConfig) && result;

  const HA::EntityConfig lightConfig = HA::EntityConfig::sensor(
    entityNameLight, valTplLight, unitLux,
    HA::StateClass::measurement, HA::DeviceClass::illuminance, iconBright);
  result = doPublishCanDeviceEntityDiscovery(entitySubLight, lightConfig, canDevConfig) && result;

  HA::EntityConfig connConfig = HA::EntityConfig::binarySensor(
    HA::connName, HA::connValueTpl, HA::connPayloadOn, HA::connPayloadOff, HA::DeviceClass::connectivity);
  HA::CanDeviceConfig connDevConfig = canDevConfig;
  connDevConfig.dataSubtopic    = getCanAvailTopic() + (MqttTopics::getSenderTopicBufSize() - 1U);
  connDevConfig.skipCanAvailability = true;
  result = doPublishCanDeviceEntityDiscovery(entitySubConn, connConfig, connDevConfig) && result;

  return result;
}

void CanAlertDriver::processMessageArrived(JsonDocument& payloadJson) { // NOLINT(readability-convert-member-functions-to-static)
  JsonVariant soundJsonVar = payloadJson[F("Sound")];
  JsonVariant volumeJsonVar = payloadJson[F("Volume")];
  JsonVariant colorsJsonVar = payloadJson[F("Colors")];
  uint8_t colorsOffset = 0U;
  uint8_t canData[8] = {0U};

  if(soundJsonVar.is<uint16_t>() && volumeJsonVar.is<uint8_t>()) {
    const uint16_t sound = soundJsonVar.as<uint16_t>();
    const uint8_t volume = volumeJsonVar.as<uint8_t>();
    canData[0] = static_cast<uint8_t>(sound & 0xFF);
    canData[1] = static_cast<uint8_t>((sound >> 8U) & 0xFF);
    canData[2] = volume;
    colorsOffset = 3U;
  }

  if(colorsJsonVar.is<JsonArray>()) {
    JsonArray colorsArray = colorsJsonVar.as<JsonArray>();
    if(colorsArray.size() == 3U) {
      bool validArray = true;
      for(uint8_t i = 0U; i < 3U; ++i) {
        if(colorsArray[i].is<uint8_t>()) {
          canData[i + colorsOffset] = colorsArray[i].as<uint8_t>();
        } else {
          validArray = false;
          break;
        }
      }
      if(validArray) {
        (void)CanBase::sendCanFrame((colorsOffset == 0U) ? CanCmd::RGB_LED : CanCmd::PLAY_MP3, canData);
      }
    }
  }
}

void CanAlertDriver::processCanFrameArrived(const CanHandler::CanFrame& canFrame) {
  switch(canFrame.cmd) {
    case static_cast<uint16_t>(CanCmd::READ_HUM_TEMP_LDR): {
      const float temperature = static_cast<float>(static_cast<uint16_t>(canFrame.data[0]) | (static_cast<uint16_t>(canFrame.data[1]) << 8U)) / 100.0F + tempOffset;
      const uint16_t humidity = static_cast<uint16_t>(canFrame.data[2]) | (static_cast<uint16_t>(canFrame.data[3]) << 8U);
      const uint16_t light = static_cast<uint16_t>(canFrame.data[4]) | (static_cast<uint16_t>(canFrame.data[5]) << 8U);
      char dataOut[dataOutBufSize] = {'\0'};
      const int32_t dataOutSize = snprintf_P(dataOut, sizeof(dataOut), humTempLdrFrame, temperature, humidity, light);
      const bool dataOutValid = (dataOutSize >= 0 && dataOutSize < static_cast<int32_t>(sizeof(dataOut)));
      if(!dataOutValid) { return; }
      (void)MqttBase::sendMessage(dataOut);
    } break;
    default: {} break;
  }
}