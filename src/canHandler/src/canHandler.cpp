#include "canHandler.hpp"
#include <CAN.h>                              /// CAN controller library.

const char CanHandler::OK_STATE[] PROGMEM                 = " [OK]";                    // OK status.
const char CanHandler::ERR_STATE[] PROGMEM                = " [ERR]";                   // Error status.
const char CanHandler::CAN_PREFIX[] PROGMEM               = "[CAN] ";

CanHandler::CanHandler(HardwareSerial& serial, bool subClassHandling) :
  serialPort(serial),
  subClassHandling_(subClassHandling) {}

bool CanHandler::begin(uint32_t canBaud) {
  serialPort.printf_P(PSTR("%s Hardware init started...\r\n"), CAN_PREFIX);
  const bool canBeginResult = CAN.begin(canBaud) == 1;
  serialPort.printf_P(PSTR("%sInit:%s\r\n"), CAN_PREFIX, (canBeginResult ? OK_STATE : ERR_STATE));
  if(!canBeginResult) { return false; }
  CAN.onReceive(rxInterrupt);
  canRxQueue = xQueueCreate(canRxQueueSize, sizeof(CanFrame));  // Create FIFO queue for RX CAN packets.
  const bool rxQueueResult = canRxQueue != nullptr;             // Check queue creation.
  canTxQueue = xQueueCreate(canTxQueueSize, sizeof(CanFrame));  // Create FIFO queue for TX CAN packets.
  const bool txQueueResult = canTxQueue != nullptr;
  serialPort.printf_P(PSTR("%sCreating queues:%s\r\n"), CAN_PREFIX, (rxQueueResult && txQueueResult ? OK_STATE : ERR_STATE));
  if(!rxQueueResult || !txQueueResult) { return false; }
  if(subClassHandling_) {
    serialPort.printf_P(PSTR("%sInit registered objects:\r\n"), CAN_PREFIX);
    for(std::size_t i = 0; i < canDevices.size(); ++i) {
      const auto& currentObject = canDevices[i];
      if(currentObject != nullptr) {
        const bool beginResult = currentObject->begin();
        serialPort.printf_P(PSTR("  %zu. %s ->%s\r\n"), i, currentObject->getCanId(), beginResult ? OK_STATE : ERR_STATE);
      }
      else {
        serialPort.printf_P(PSTR("  %zu. No object here!\r\n"), i);
      }
    }
  }
  return true;
}

void CanHandler::rxInterrupt(int packetsNum) {
  if(packetsNum <= 0) { return; }
  if(!CAN.packetExtended()) { return; }
  CanFrame rxCanData;
  rxCanData.canId.id = CAN.packetId();
  if(!CAN.packetRtr()) {
    const uint8_t canDataDlc = static_cast<uint8_t>(CAN.packetDlc());
    const uint8_t bytesReaded = static_cast<uint8_t>(CAN.readBytes(rxCanData.data, canDataDlc));
    if(canDataDlc != bytesReaded) { return; }
  }
  if(xQueueSend(canRxQueue, &rxCanData, 50U) != pdTRUE) { return; }
}

bool CanHandler::send(const CanFrame& frameOut) {
  const bool qSendResult = xQueueSend(canTxQueue, &frameOut, 50U) == pdTRUE;
  return qSendResult;
}

bool CanHandler::loop() {
  { // Handle received CAN frame.
    CanFrame frameIn;
    const bool qReceiveResult = xQueueReceive(canRxQueue, &frameIn, 0U) == pdTRUE;
    if(qReceiveResult) {
      const uint32_t messageCanId = frameIn.canId.id;
      for(const auto &currentObject : canDevices) {
        if(currentObject == nullptr) { return false; }
        if(currentObject->getCanId() == messageCanId) {
          currentObject->canFrameReceived(frameIn);
        }
      }
    }
  }
  // Handle the looping of subclasses.
  if(subClassHandling_) {
    for(const auto &currentObject : canDevices) {
      if(currentObject != nullptr) {
        currentObject->loop();
      }
    }
  }
  { // Handle CAN frame sending.
    CanFrame frameOut;
    const bool qReceiveResult = xQueueReceive(canTxQueue, &frameOut, 0U) == pdTRUE;
    if(qReceiveResult) {
      const bool beginPacketResult = CAN.beginExtendedPacket(frameOut.canId.id) > 0;
      if(!beginPacketResult) { return false; }
      const bool packetWriteResult = CAN.write(frameOut.data, sizeof(frameOut.data)) > 0;
      if(!packetWriteResult) { return false; }
      const bool endPacketResult = CAN.endPacket() > 0;
      if(!endPacketResult) { return false; }
    }
  }
  return true;
}

bool CanHandler::registerCallback(CanHandler::CanComBase* obj) {
  if(!obj) { return false; }
  canDevices.push_back(obj);
  return true;
}

//////////////////// -- SoftwareTimer class-- ////////////////////

CanHandler::SoftwareTimer::SoftwareTimer(uint32_t time) : time_(time), start_time_(0) {}

bool CanHandler::SoftwareTimer::isExpired() { return (millis() - start_time_ >= time_); }

void CanHandler::SoftwareTimer::reload() { start_time_ = millis(); }

//////////////////// -- CanComBase class-- ////////////////////

CanHandler::CanComBase::CanComBase(CanHandler& canHandler, uint32_t canId) :
  canHandler(canHandler),
  nodeCanId(canId),
  pingTimer(pingTime),
  alertTimer(alertTime)
{
  canHandler.registerCallback(this);
}

bool CanHandler::CanComBase::begin() {
  sendCanCmd(CanCmd::PING);
  pingTimer.reload();
  alertTimer.reload();
  return true;
}

bool CanHandler::CanComBase::loop() {
  if(pingTimer.isExpired()) {
    pingTimer.reload();
    sendCanCmd(CanCmd::PING);
  }
  return alertTimer.isExpired();
}

void CanHandler::CanComBase::canFrameReceived(CanHandler::CanFrame& canFrame) {
  pingTimer.reload();
  alertTimer.reload();
}

const uint32_t CanHandler::CanComBase::getCanId() const { return nodeCanId; }

void CanHandler::CanComBase::sendCanFrame(CanHandler::CanFrame& canFrame) const {
  canHandler.send(canFrame);
}

void CanHandler::CanComBase::sendCanCmd(CanCmd command) {
  CanFrame pingFrame;
  pingFrame.canId.from = localCanId;
  pingFrame.canId.to = nodeCanId;
  pingFrame.canId.cmd = static_cast<uint16_t>(command);
  sendCanFrame(pingFrame);
}
