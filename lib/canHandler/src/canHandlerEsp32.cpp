#ifdef ESP32
#include "canHandlerEsp32.hpp"
#include "CAN.h"                                                    /// CAN controller library.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include <cstdlib>                                                  /// Standard library for memory and utilities.
#include "common.hpp"                                               /// Common definitions and functions.

QueueHandle_t CanHandlerEsp32::canRxQueue = xQueueCreate(canRxQueueSize, sizeof(CanFrame));

CanHandlerEsp32::CanHandlerEsp32() : // NOLINT(modernize-use-equals-default)
  canTxQueue(xQueueCreate(canTxQueueSize, sizeof(CanFrame))),
  canDevicesList{},
  canDevicesListMutex(xSemaphoreCreateMutex())
{}

bool CanHandlerEsp32::init(uint32_t canBaud) {
  { // Setup mutex and message queues.
    if(canDevicesListMutex == nullptr) {
      Logger::get().printf_P(PSTR("[CAN] Mutex is not initialized properly!\r\n"));
      return false;
    }
    //configASSERT(canRxQueue != nullptr);                          // Assert if the queue creation fails.
    //configASSERT(canTxQueue != nullptr);
    const bool rxQueueResult = (canRxQueue != nullptr);           // Check queue creation.
    const bool txQueueResult = (canTxQueue != nullptr);
    Logger::get().printf_P(PSTR("[CAN] Creating queues:\r\n  RX -> %s\r\n  TX -> %s\r\n"),
      Str::getStateStr(rxQueueResult), Str::getStateStr(txQueueResult));
    if(!rxQueueResult || !txQueueResult) { return false; }
  }
#if defined(NEW_CAN_ADDRESS) && defined(MASTER_CAN_ADDRESS)
  // Save new CAN IDs.
  static constexpr uint16_t newMasterCanId = static_cast<uint16_t>(MASTER_CAN_ADDRESS);
  static constexpr uint16_t newLocalCanId = static_cast<uint16_t>(NEW_CAN_ADDRESS);
  const bool canIdsSavingResult = saveCanIds(newMasterCanId, newLocalCanId);
  Logger::get().printf_P(PSTR("[CAN] Saving new IDs: %s\r\n  Master: %hu\r\n  Local: %hu\r\n"),
    Str::getStateStr(canIdsSavingResult), newMasterCanId, newLocalCanId);
  if(!canIdsSavingResult) { return false; }
#endif
  { // Load CAN ID's.
  const bool canIdLoadingResult = loadCanIds();
  Logger::get().printf_P(PSTR("[CAN] Loading IDs: %s\r\n  Master: %hu\r\n  Local: %hu\r\n"),
    Str::getStateStr(canIdLoadingResult), getMasterCanId(), getLocalCanId());
  if(!canIdLoadingResult) { return false; }
  }
  { // Initialise CAN peripheral.
    const bool canBeginResult = CAN.begin(canBaud) == 1;
    Logger::get().printf_P(PSTR("[CAN] Init:%s\r\n"), Str::getStateStr(canBeginResult));
    CAN.onReceive(rxInterrupt);
    if(!canBeginResult) { return false; }
  }
  { // Set up the CAN filtering.
    const bool setFilterResult = CAN.filterExtended(
      CanHandlerBase::getCanFilteredId(), CanHandlerBase::getCanIdFilterMask()) == 1;
    Logger::get().printf_P(PSTR("[CAN] Set up filter:%s\r\n"), Str::getStateStr(setFilterResult));
    if(!setFilterResult) { return false; }
  }
  // List CAN devices.
  Logger::get().printf_P(PSTR("[CAN] Drivers for devices:\r\n"));
  if(xSemaphoreTake(canDevicesListMutex, semaphoreTimeout) == pdTRUE) {
    for(std::size_t i = 0U; i < canDevicesList.size(); ++i) {
      if(canDevicesList[i] != nullptr) {
        Logger::get().printf_P(PSTR("  %zu. %hu\r\n"), i, canDevicesList[i]->getClientCanId());
      } else {
        Logger::get().printf_P(PSTR("  %zu. No object here!\r\n"), i);
      }
    }
    xSemaphoreGive(canDevicesListMutex);
  }
  return true;
}

bool CanHandlerEsp32::send(uint16_t command, const uint8_t (&data)[8]) const {
  if(isDeviceMaster()) { return false; }
  return send(CanFrame{getMasterCanId(), command, getLocalCanId(), data});
}

bool CanHandlerEsp32::send(const CanFrame& frameOut) const {
  return (xQueueSend(canTxQueue, &frameOut, canTxQueueTimeout) == pdTRUE);
}

void CanHandlerEsp32::rxInterrupt(int packetsNum) { // NOLINT(readability-convert-member-functions-to-static)
  if(packetsNum <= 0) { return; }
  CanFrame rxCanData;
  rxCanData.extId = CAN.packetId();
  if(!CAN.packetRtr()) {
    const uint8_t canDataDlc = static_cast<uint8_t>(CAN.packetDlc());
    const uint8_t bytesReaded = static_cast<uint8_t>(CAN.readBytes(rxCanData.data, canDataDlc));
    if(canDataDlc != bytesReaded) { return; }
  }
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if(xQueueSendFromISR(canRxQueue, &rxCanData, &xHigherPriorityTaskWoken) != pdTRUE) {
    return;
  }
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

bool CanHandlerEsp32::run() {
  { // Handle received CAN frame.
    CanFrame frameIn;
    if(xQueueReceive(canRxQueue, &frameIn, static_cast<TickType_t>(0U)) == pdTRUE) {
      const uint16_t nodeCanId = static_cast<uint16_t>(frameIn.from);
      if(xSemaphoreTake(canDevicesListMutex, semaphoreTimeout) == pdTRUE) {
      // Logger::get().printf_P(PSTR("[CAN] Receiving: %hu | %hu | %hu\r\n"), frameIn.to, frameIn.cmd, frameIn.from);
        for(const auto &currentcanDevice : canDevicesList) {
          if(currentcanDevice == nullptr) { continue; }
          if(currentcanDevice->getClientCanId() == nodeCanId) {
            currentcanDevice->canFrameArrivedCallback(frameIn);
            break;
          }
        }
        xSemaphoreGive(canDevicesListMutex);
      }
    }
  }
  { // Handle CAN frame sending.
    CanFrame frameOut;
    if(xQueueReceive(canTxQueue, &frameOut, static_cast<TickType_t>(0U)) == pdTRUE) {
      const bool beginPacketResult = CAN.beginExtendedPacket(frameOut.extId, sizeof(frameOut.data)) > 0;
      if(!beginPacketResult) { return false; }
      const bool packetWriteResult = CAN.write(frameOut.data, sizeof(frameOut.data)) > 0U;
      if(!packetWriteResult) { return false; }
      const bool endPacketResult = CAN.endPacket() > 0;
      if(!endPacketResult) { return false; }
      // Logger::get().printf_P(PSTR("[CAN] Sending: %hu | %hu | %hu\r\n"), frameOut.to, frameOut.cmd, frameOut.from);
    }
  }
  return true;
}

bool CanHandlerEsp32::registerCallback(CanBase* canBasePtr) { // NOLINT(readability-convert-member-functions-to-static)
  if(canBasePtr == nullptr) { return false; }
  if(xSemaphoreTake(canDevicesListMutex, semaphoreTimeout) == pdTRUE) {
    canDevicesList.push_back(canBasePtr);
    xSemaphoreGive(canDevicesListMutex);
    return true;
  }
  return false;
}
#endif // ESP32