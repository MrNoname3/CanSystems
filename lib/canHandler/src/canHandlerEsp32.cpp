#if defined(ESP32) || defined(NATIVE_TEST)
#include "canHandlerEsp32.hpp"
#include "common.hpp"                                               /// Common definitions and functions.

QueueHandle_t CanHandlerEsp32::canRxQueue = xQueueCreate(canRxQueueSize, sizeof(CanFrame));
volatile uint32_t CanHandlerEsp32::rxIncompleteFrames = 0U;
volatile uint32_t CanHandlerEsp32::rxQueueFullFrames = 0U;

ESP32SJA1000* CanHandlerEsp32::isrController = nullptr;

CanHandlerEsp32::CanHandlerEsp32(ESP32SJA1000& controller) :
  controller(controller),
  canTxQueue(xQueueCreate(canTxQueueSize, sizeof(CanFrame))),
  canDevicesListMutex(xSemaphoreCreateMutex()) {
  isrController = &controller;
}

bool CanHandlerEsp32::init(uint32_t canBaud) {
  { // Setup mutex and message queues.
    if(canDevicesListMutex == nullptr) {
      Logger::get()->printf_P(PSTR("[CAN] Mutex is not initialized properly!\r\n"));
      return false;
    }
    // configASSERT(canRxQueue != nullptr);                          // Assert if the queue creation fails.
    // configASSERT(canTxQueue != nullptr);
    const bool rxQueueResult = (canRxQueue != nullptr);           // Check queue creation.
    const bool txQueueResult = (canTxQueue != nullptr);
    Logger::get()->printf_P(PSTR("[CAN] Creating queues:\r\n  RX -> %s\r\n  TX -> %s\r\n"),
                            Str::getStateStr(rxQueueResult), Str::getStateStr(txQueueResult));
    if(!rxQueueResult || !txQueueResult) { return false; }
  }
#if defined(NEW_CAN_ADDRESS) && defined(MASTER_CAN_ADDRESS)
  // Save new CAN IDs.
  static constexpr uint16_t newMasterCanId = static_cast<uint16_t>(MASTER_CAN_ADDRESS);
  static constexpr uint16_t newLocalCanId = static_cast<uint16_t>(NEW_CAN_ADDRESS);
  const bool canIdsSavingResult = saveCanIds(newMasterCanId, newLocalCanId);
  Logger::get()->printf_P(PSTR("[CAN] Saving new IDs: %s\r\n  Master: %hu\r\n  Local: %hu\r\n"),
                          Str::getStateStr(canIdsSavingResult), newMasterCanId, newLocalCanId);
  if(!canIdsSavingResult) { return false; }
#endif
  { // Load CAN ID's.
    const bool canIdLoadingResult = loadCanIds();
    Logger::get()->printf_P(PSTR("[CAN] Loading IDs: %s\r\n  Master: %hu\r\n  Local: %hu\r\n"),
                            Str::getStateStr(canIdLoadingResult), getMasterCanId(), getLocalCanId());
    if(!canIdLoadingResult) { return false; }
  }
  { // Initialise CAN peripheral.
    const bool canBeginResult = controller.begin(canBaud) == 1U;
    Logger::get()->printf_P(PSTR("[CAN] Init:%s\r\n"), Str::getStateStr(canBeginResult));
    controller.onReceive(rxInterrupt);
    if(!canBeginResult) { return false; }
  }
  { // Set up the CAN filtering.
    const bool setFilterResult = controller.filterExtended(CanHandlerBase::getCanFilteredId(), CanHandlerBase::getCanIdFilterMask()) == 1U;
    Logger::get()->printf_P(PSTR("[CAN] Set up filter:%s\r\n"), Str::getStateStr(setFilterResult));
    if(!setFilterResult) { return false; }
  }
  // List CAN devices, and check the ids they claim. This is the first moment that can be done:
  // a device registers from its own constructor, which runs before anything has been read out of
  // the EEPROM, so the check it makes there has nothing to compare against yet.
  Logger::get()->printf_P(PSTR("[CAN] Drivers for devices:\r\n"));
  bool deviceIdsFree = true;
  if(xSemaphoreTake(canDevicesListMutex, semaphoreTimeout) == pdTRUE) {
    uint8_t deviceIndex = 0U;
    for(CanBase* d = deviceList.first(); d != nullptr; d = d->getNext()) {
      const uint16_t clientCanId = d->getClientCanId();
      const bool reserved = (clientCanId == getLocalCanId()) || (clientCanId == getMasterCanId());
      Logger::get()->printf_P(PSTR("  %hhu. %hu%s\r\n"), deviceIndex++, clientCanId,
                              reserved ? PSTR(" <- reserved id!") : PSTR(""));
      if(reserved) { deviceIdsFree = false; }
    }
    xSemaphoreGive(canDevicesListMutex);
  }
  return deviceIdsFree;
}

bool CanHandlerEsp32::send(uint16_t command, const uint8_t (&data)[8]) const {
  if(isDeviceMaster()) { return false; }
  return send(CanFrame{ getMasterCanId(), command, getLocalCanId(), data });
}

bool CanHandlerEsp32::send(const CanFrame& frameOut) const {
  return (xQueueSend(canTxQueue, &frameOut, canTxQueueTimeout) == pdTRUE);
}

void CanHandlerEsp32::rxInterrupt(int packetsNum) { // NOLINT(readability-convert-member-functions-to-static)
  if((packetsNum <= 0) || (isrController == nullptr)) { return; }
  CanFrame rxCanData;
  rxCanData.extId = isrController->packetId();
  if(!isrController->packetRtr()) {
    const uint8_t canDataDlc = isrController->packetDlc();
    const uint8_t bytesReaded = static_cast<uint8_t>(isrController->readBytes(rxCanData.data, canDataDlc));
    if(canDataDlc != bytesReaded) {
      ++rxIncompleteFrames;
      return;
    }
  }
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if(xQueueSendFromISR(canRxQueue, &rxCanData, &xHigherPriorityTaskWoken) != pdTRUE) {
    ++rxQueueFullFrames;
    return;                                                         // No yield: nothing was queued, so no task became ready.
  }
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

bool CanHandlerEsp32::run() {
  if(controller.isBusOff()) {
    Logger::get()->printf_P(PSTR("[CAN] Bus-off detected, recovering\r\n"));
    controller.recoverFromBusOff();
  }
  reportDroppedFrames();
  { // Handle received CAN frames.
    // The mutex is taken around the whole pass rather than per frame: it guards the device list,
    // which only changes at registration time. Taking it first also means a timeout leaves the
    // frames queued instead of dequeueing one and dropping it.
    if(xSemaphoreTake(canDevicesListMutex, semaphoreTimeout) == pdTRUE) {
      CanFrame frameIn;
      (void)CanFramePump::drain(
          [&frameIn]() -> bool { return xQueueReceive(canRxQueue, &frameIn, static_cast<TickType_t>(0U)) == pdTRUE; },
          [this, &frameIn]() -> bool { dispatchRxFrame(frameIn); return true; },
          maxFramesPerRun);
      xSemaphoreGive(canDevicesListMutex);
    }
  }
  { // Handle CAN frame sending.
    CanFrame frameOut;
    // The controller holds one frame at a time, so a busy transmit buffer ends the pass with the
    // queue intact rather than waiting for the bus inside the loop task.
    const CanFramePump::Result txResult = CanFramePump::drain(
        [this, &frameOut]() -> bool {
          if(!controller.txReady()) { return false; }
          return xQueueReceive(canTxQueue, &frameOut, static_cast<TickType_t>(0U)) == pdTRUE;
        },
        [this, &frameOut]() -> bool { return transmitFrame(frameOut); },
        maxFramesPerRun);
    if(txResult.failed) { return false; }
  }
  return true;
}

void CanHandlerEsp32::reportDroppedFrames() {
  const uint32_t incomplete = rxIncompleteReporter.takeGrowth(rxIncompleteFrames);
  const uint32_t queueFull = rxQueueFullReporter.takeGrowth(rxQueueFullFrames);
  if((incomplete != 0U) || (queueFull != 0U)) {
    Logger::get()->printf_P(PSTR("[CAN] RX dropped: %u incomplete, %u queue full\r\n"), incomplete, queueFull);
  }
  // endPacket() hands the frame over without waiting for it, so this is where a frame the bus
  // never took is reported.
  const uint32_t abandoned = txAbandonedReporter.takeGrowth(controller.getAbandonedTxFrames());
  if(abandoned != 0U) {
    Logger::get()->printf_P(PSTR("[CAN] TX abandoned: %u frames the bus did not take\r\n"), abandoned);
  }
}

void CanHandlerEsp32::dispatchRxFrame(const CanFrame& frameIn) const { // NOLINT(readability-convert-member-functions-to-static)
  // Logger::get()->printf_P(PSTR("[CAN] Receiving: %hu | %hu | %hu\r\n"), frameIn.to, frameIn.cmd, frameIn.from);
  const uint16_t nodeCanId = static_cast<uint16_t>(frameIn.from);
  CanBase* device = deviceList.findIf([nodeCanId](const CanBase* d) -> bool { return d->getClientCanId() == nodeCanId; });
  if(device != nullptr) {
    device->canFrameArrivedCallback(frameIn);
  } else if(unclaimedFrameCallback != nullptr) {
    unclaimedFrameCallback(unclaimedFrameContext, frameIn);
  }
}

void CanHandlerEsp32::setUnclaimedFrameCallback(void (*callback)(void*, const CanFrame&), void* context) {
  unclaimedFrameCallback = callback;
  unclaimedFrameContext = context;
}

bool CanHandlerEsp32::transmitFrame(const CanFrame& frameOut) const { // NOLINT(readability-convert-member-functions-to-static)
  const bool beginPacketResult = controller.beginExtendedPacket(frameOut.extId, sizeof(frameOut.data)) != 0U;
  const bool packetWriteResult = beginPacketResult && (controller.write(frameOut.data, sizeof(frameOut.data)) == sizeof(frameOut.data));
  const bool endPacketResult = packetWriteResult && (controller.endPacket() != 0U);
  if(!endPacketResult) {
    // The frame is already consumed from the queue, so a TX failure would otherwise vanish
    // silently (the mains discard the runTasks() failure mask).
    Logger::get()->printf_P(PSTR("[CAN] TX failed: to=%u cmd=%u from=%u\r\n"), static_cast<uint32_t>(frameOut.to), static_cast<uint32_t>(frameOut.cmd), static_cast<uint32_t>(frameOut.from));
    return false;
  }
  // Logger::get()->printf_P(PSTR("[CAN] Sending: %hu | %hu | %hu\r\n"), frameOut.to, frameOut.cmd, frameOut.from);
  return true;
}

bool CanHandlerEsp32::isClientIdRegistered(uint16_t clientCanId) const { // NOLINT(readability-convert-member-functions-to-static)
  if(xSemaphoreTake(canDevicesListMutex, semaphoreTimeout) != pdTRUE) { return true; }  // Unknown: answer "taken".
  const CanBase* device = deviceList.findIf([clientCanId](const CanBase* d) -> bool { return d->getClientCanId() == clientCanId; });
  xSemaphoreGive(canDevicesListMutex);
  return device != nullptr;
}

bool CanHandlerEsp32::registerCallback(CanBase* canBasePtr) { // NOLINT(readability-convert-member-functions-to-static)
  if(xSemaphoreTake(canDevicesListMutex, semaphoreTimeout) != pdTRUE) { return false; }
  const bool appendResult = deviceList.append(canBasePtr);
  xSemaphoreGive(canDevicesListMutex);
  return appendResult;
}
#endif // ESP32 || NATIVE_TEST
