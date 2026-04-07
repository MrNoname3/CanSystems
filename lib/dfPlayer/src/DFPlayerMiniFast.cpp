#include "DFPlayerMiniFast.h"

bool DFPlayerMiniFast::begin(Stream& stream, bool debug, uint16_t threshold) {
  timeoutTime = threshold;
  serial = &stream;
  this->debug = debug;
  sendStack.start_byte = static_cast<uint8_t>(PacketValues::SB);
  sendStack.version    = static_cast<uint8_t>(PacketValues::VER);
  sendStack.length     = static_cast<uint8_t>(PacketValues::LEN);
  sendStack.end_byte   = static_cast<uint8_t>(PacketValues::EB);
  return true;
}

void DFPlayerMiniFast::playNext() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::NEXT);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::playPrevious() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::PREV);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::play(uint16_t trackNum) {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::PLAY);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = static_cast<uint8_t>((trackNum >> 8U) & 0xFFU);
  sendStack.paramLSB = static_cast<uint8_t>(trackNum & 0xFFU);
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::stop() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::STOP);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 0U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::playFromMP3Folder(uint16_t trackNum) {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::USE_MP3_FOLDER);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = static_cast<uint8_t>((trackNum >> 8U) & 0xFFU);
  sendStack.paramLSB = static_cast<uint8_t>(trackNum & 0xFFU);
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::playAdvertisement(uint16_t trackNum) {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::INSERT_ADVERT);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = static_cast<uint8_t>((trackNum >> 8U) & 0xFFU);
  sendStack.paramLSB = static_cast<uint8_t>(trackNum & 0xFFU);
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::stopAdvertisement() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::STOP_ADVERT);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 0U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::incVolume() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::INC_VOL);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::decVolume() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::DEC_VOL);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::volume(uint8_t level) {
  if(level <= 30U) {
    sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::VOLUME);
    sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
    sendStack.paramMSB = 0U;
    sendStack.paramLSB = level;
    findChecksum(sendStack);
    sendData();
  }
}

void DFPlayerMiniFast::EQSelect(uint8_t setting) {
  if(setting <= 5U) {
    sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::EQ);
    sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
    sendStack.paramMSB = 0U;
    sendStack.paramLSB = setting;
    findChecksum(sendStack);
    sendData();
  }
}

void DFPlayerMiniFast::loop(uint16_t trackNum) {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::PLAYBACK_MODE);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = static_cast<uint8_t>((trackNum >> 8U) & 0xFFU);
  sendStack.paramLSB = static_cast<uint8_t>(trackNum & 0xFFU);
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::playbackSource(uint8_t source) {
  if((source > 0U) && (source <= 5U)) {
    sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::PLAYBACK_SRC);
    sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
    sendStack.paramMSB = 0U;
    sendStack.paramLSB = source;
    findChecksum(sendStack);
    sendData();
  }
}

void DFPlayerMiniFast::standbyMode() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::STANDBY);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::normalMode() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::NORMAL);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::reset() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::RESET);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::resume() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::PLAYBACK);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::pause() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::PAUSE);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::playFolder(uint8_t folderNum, uint8_t trackNum) {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::SPEC_FOLDER);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = folderNum;
  sendStack.paramLSB = trackNum;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::playLargeFolder(uint8_t folderNum, uint16_t trackNum) {
  // Encode folder (4 bits) and track (12 bits) into a single 16-bit argument.
  const uint16_t arg = (static_cast<uint16_t>(folderNum) << 12U) | (trackNum & 0xFFFU);
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::SPEC_TRACK_3000);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = static_cast<uint8_t>(arg >> 8U);
  sendStack.paramLSB = static_cast<uint8_t>(arg & 0xFFU);
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::volumeAdjustSet(uint8_t gain) {
  if(gain <= 31U) {
    sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::VOL_ADJ);
    sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
    sendStack.paramMSB = 0U;
    sendStack.paramLSB = static_cast<uint8_t>(BaseVolumeAdjustValue::VOL_ADJUST) + gain;
    findChecksum(sendStack);
    sendData();
  }
}

void DFPlayerMiniFast::startRepeatPlay() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::REPEAT_PLAY);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = static_cast<uint8_t>(RepeatPlayValues::START_REPEAT);
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::stopRepeatPlay() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::REPEAT_PLAY);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = static_cast<uint8_t>(RepeatPlayValues::STOP_REPEAT);
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::repeatFolder(uint16_t folder) {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::REPEAT_FOLDER);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = static_cast<uint8_t>((folder >> 8U) & 0xFFU);
  sendStack.paramLSB = static_cast<uint8_t>(folder & 0xFFU);
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::randomAll() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::RANDOM_ALL);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 0U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::startRepeat() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::REPEAT_CURRENT);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 0U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::stopRepeat() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::REPEAT_CURRENT);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::startDAC() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::SET_DAC);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 0U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::stopDAC() {
  sendStack.commandValue  = static_cast<uint8_t>(ControlCommandValues::SET_DAC);
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = 0U;
  sendStack.paramLSB = 1U;
  findChecksum(sendStack);
  sendData();
}

void DFPlayerMiniFast::sleep() {
  playbackSource(static_cast<uint8_t>(PlaybackSourceValues::SLEEP));
}

void DFPlayerMiniFast::wakeUp() {
  playbackSource(static_cast<uint8_t>(PlaybackSourceValues::TF));
}

bool DFPlayerMiniFast::isPlaying() {
  const int16_t result = query(static_cast<uint8_t>(QueryCommandValues::GET_STATUS_));
  if(result != -1) {
    return (result & 1) != 0;
  }
  return false;
}

int16_t DFPlayerMiniFast::currentVolume() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_VOL));
}

int16_t DFPlayerMiniFast::currentEQ() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_EQ));
}

int16_t DFPlayerMiniFast::currentMode() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_MODE));
}

int16_t DFPlayerMiniFast::currentVersion() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_VERSION));
}

int16_t DFPlayerMiniFast::numUsbTracks() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_TF_FILES));
}

int16_t DFPlayerMiniFast::numSdTracks() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_U_FILES));
}

int16_t DFPlayerMiniFast::numFlashTracks() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_FLASH_FILES));
}

int16_t DFPlayerMiniFast::currentUsbTrack() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_TF_TRACK));
}

int16_t DFPlayerMiniFast::currentSdTrack() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_U_TRACK));
}

int16_t DFPlayerMiniFast::currentFlashTrack() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_FLASH_TRACK));
}

int16_t DFPlayerMiniFast::numTracksInFolder(uint8_t folder) {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_FOLDER_FILES), 0U, folder);
}

int16_t DFPlayerMiniFast::numFolders() {
  return query(static_cast<uint8_t>(QueryCommandValues::GET_FOLDERS));
}

void DFPlayerMiniFast::setTimeout(uint16_t threshold) {
  timeoutTime = threshold;
}

void DFPlayerMiniFast::printError() {
  if(recStack.commandValue == 0x40U) {
    switch(recStack.paramLSB) {

      case 0x1U: {
        Serial.println(F("Module busy (this info is returned when the initialization is not done)"));
      } break;

      case 0x2U: {
        Serial.println(F("Currently sleep mode(supports only specified device in sleep mode)"));
      } break;

      case 0x3U: {
        Serial.println(F("Serial receiving error(a frame has not been received completely yet)"));
      } break;

      case 0x4U: {
        Serial.println(F("Checksum incorrect"));
      } break;

      case 0x5U: {
        Serial.println(F("Specified track is out of current track scope"));
      } break;

      case 0x6U: {
        Serial.println(F("Specified track is not found"));
      } break;

      case 0x7U: {
        Serial.println(F("Insertion error(an inserting operation only can be done when a track is being played)"));
      } break;

      case 0x8U: {
        Serial.println(F("SD card reading failed(SD card pulled out or damaged)"));
      } break;

      case 0xAU: {
        Serial.println(F("Entered into sleep mode"));
      } break;

      default: {
        Serial.print(F("Unknown error: "));
        Serial.println(recStack.paramLSB);
      } break;

    }
  }
  else {
    Serial.println(F("No error"));
  }
}

void DFPlayerMiniFast::findChecksum(Stack& _stack) {
  // Two's complement negation of the sum of bytes: version, length, cmd, feedback, MSB param, LSB param.
  const uint16_t checksum = static_cast<uint16_t>(~(_stack.version + _stack.length + _stack.commandValue + _stack.feedbackValue + _stack.paramMSB + _stack.paramLSB) + 1);
  _stack.checksumMSB = static_cast<uint8_t>(checksum >> 8U);
  _stack.checksumLSB = static_cast<uint8_t>(checksum & 0xFFU);
}

void DFPlayerMiniFast::sendData() {
  serial->write(sendStack.start_byte);
  serial->write(sendStack.version);
  serial->write(sendStack.length);
  serial->write(sendStack.commandValue);
  serial->write(sendStack.feedbackValue);
  serial->write(sendStack.paramMSB);
  serial->write(sendStack.paramLSB);
  serial->write(sendStack.checksumMSB);
  serial->write(sendStack.checksumLSB);
  serial->write(sendStack.end_byte);

  if(debug) {
    Serial.print(F("Sent "));
    printStack(sendStack);
    Serial.println();
  }
}

void DFPlayerMiniFast::flush() {
  while(serial->available()) {
    serial->read();
  }
}

int16_t DFPlayerMiniFast::query(uint8_t cmd, uint8_t msb, uint8_t lsb) {
  flush();
  sendStack.commandValue  = cmd;
  sendStack.feedbackValue = static_cast<uint8_t>(PacketValues::NO_FEEDBACK);
  sendStack.paramMSB = msb;
  sendStack.paramLSB = lsb;
  findChecksum(sendStack);
  sendData();
  timeoutTimer = millis();

  if(parseFeedback()) {
    if(recStack.commandValue != 0x40U) {
      return static_cast<int16_t>((static_cast<uint16_t>(recStack.paramMSB) << 8U) | recStack.paramLSB);
    }
  }
  return -1;
}

void DFPlayerMiniFast::printStack(Stack _stack) {
  Serial.println(F("Stack:"));
  Serial.print(_stack.start_byte, HEX);    Serial.print(' ');
  Serial.print(_stack.version, HEX);       Serial.print(' ');
  Serial.print(_stack.length, HEX);        Serial.print(' ');
  Serial.print(_stack.commandValue, HEX);  Serial.print(' ');
  Serial.print(_stack.feedbackValue, HEX); Serial.print(' ');
  Serial.print(_stack.paramMSB, HEX);      Serial.print(' ');
  Serial.print(_stack.paramLSB, HEX);      Serial.print(' ');
  Serial.print(_stack.checksumMSB, HEX);   Serial.print(' ');
  Serial.print(_stack.checksumLSB, HEX);   Serial.print(' ');
  Serial.println(_stack.end_byte, HEX);
}

bool DFPlayerMiniFast::parseFeedback() {
  while(true) {
    if(serial->available()) {
      const uint8_t recChar = static_cast<uint8_t>(serial->read());

      if(debug) {
        Serial.print(F("Rec: "));
        Serial.println(recChar, HEX);
        Serial.print(F("State: "));
      }

      switch(state) {

        case Fsm::find_start_byte: {
          if(debug) { Serial.println(F("find_start_byte")); }
          if(recChar == static_cast<uint8_t>(PacketValues::SB)) {
            recStack.start_byte = recChar;
            state = Fsm::find_ver_byte;
          }
        } break;

        case Fsm::find_ver_byte: {
          if(debug) { Serial.println(F("find_ver_byte")); }
          if(recChar != static_cast<uint8_t>(PacketValues::VER)) {
            if(debug) { Serial.println(F("ver error")); }
            state = Fsm::find_start_byte;
            return false;
          }
          recStack.version = recChar;
          state = Fsm::find_len_byte;
        } break;

        case Fsm::find_len_byte: {
          if(debug) { Serial.println(F("find_len_byte")); }
          if(recChar != static_cast<uint8_t>(PacketValues::LEN)) {
            if(debug) { Serial.println(F("len error")); }
            state = Fsm::find_start_byte;
            return false;
          }
          recStack.length = recChar;
          state = Fsm::find_command_byte;
        } break;

        case Fsm::find_command_byte: {
          if(debug) { Serial.println(F("find_command_byte")); }
          recStack.commandValue = recChar;
          state = Fsm::find_feedback_byte;
        } break;

        case Fsm::find_feedback_byte: {
          if(debug) { Serial.println(F("find_feedback_byte")); }
          recStack.feedbackValue = recChar;
          state = Fsm::find_param_MSB;
        } break;

        case Fsm::find_param_MSB: {
          if(debug) { Serial.println(F("find_param_MSB")); }
          recStack.paramMSB = recChar;
          state = Fsm::find_param_LSB;
        } break;

        case Fsm::find_param_LSB: {
          if(debug) { Serial.println(F("find_param_LSB")); }
          recStack.paramLSB = recChar;
          state = Fsm::find_checksum_MSB;
        } break;

        case Fsm::find_checksum_MSB: {
          if(debug) { Serial.println(F("find_checksum_MSB")); }
          recStack.checksumMSB = recChar;
          state = Fsm::find_checksum_LSB;
        } break;

        case Fsm::find_checksum_LSB: {
          if(debug) { Serial.println(F("find_checksum_LSB")); }
          recStack.checksumLSB = recChar;

          const uint16_t recChecksum  = (static_cast<uint16_t>(recStack.checksumMSB) << 8U) | recStack.checksumLSB;
          findChecksum(recStack);
          const uint16_t calcChecksum = (static_cast<uint16_t>(recStack.checksumMSB) << 8U) | recStack.checksumLSB;

          if(recChecksum != calcChecksum) {
            if(debug) {
              Serial.println(F("checksum error"));
              Serial.print(F("recChecksum: 0x"));
              Serial.println(recChecksum, HEX);
              Serial.print(F("calcChecksum: 0x"));
              Serial.println(calcChecksum, HEX);
              Serial.println();
            }
            state = Fsm::find_start_byte;
            return false;
          }
          state = Fsm::find_end_byte;
        } break;

        case Fsm::find_end_byte: {
          if(debug) { Serial.println(F("find_end_byte")); }
          if(recChar != static_cast<uint8_t>(PacketValues::EB)) {
            if(debug) { Serial.println(F("eb error")); }
            state = Fsm::find_start_byte;
            return false;
          }
          recStack.end_byte = recChar;
          state = Fsm::find_start_byte;
          return true;
        } break;

        default: {
        } break;

      }
    }

    // Timeout: the MP3 player did not respond within the configured threshold.
    if(millis() - timeoutTimer >= timeoutTime) {
      if(debug) { Serial.println(F("timeout error")); }
      state = Fsm::find_start_byte;
      return false;
    }
  }
}
