#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.
#include <array>                                                    /// Fixed-size array with zero overhead for size 0.
#include "connectivity.hpp"                                         /// Handles the MQTT connection.
#include "dataTransfer.hpp"                                         /// Provides file transfer functionality via MQTT.
#include <ArduinoJson.h>                                            /// Handle JSON files.
#include "resetHandler.hpp"                                         /// Handles MCU reset from the program.
#include "common.hpp"                                               /// Common definitions and functions.

/// @brief Provides common MQTT functionalities.
/// @tparam MaxDynCmds Maximum number of dynamically registered commands (0 = no dynamic commands, zero overhead).
template<uint8_t MaxDynCmds = 0U>
class MqttCommon final : public MqttBase {
private:
  static constexpr uint8_t dataOutBufSize = 68U;                    // Buffer size for outgoing data messages.
  static constexpr uint8_t maxCmdLength   = 16U;                    // Maximum length of a command string.
  // Format string for version messages.
  static constexpr const char PROGMEM versionMessageFrame[] = R"({"CPP":%u,"FW":%hu,"GH":"%x","Dirty":%hu,"RR":%hu})";

  static constexpr const char PROGMEM cmdReboot[] = "reboot";       // Command name strings stored in flash.

public:
  /// @brief Constructs a new MqttCommon object.
  /// @param connectivity Reference to a Connectivity object to manage MQTT connections.
  /// @param subtopic Subtopic for MQTT message routing.
  MqttCommon(Connectivity& connectivity, const char* subtopic) :
    MqttBase(connectivity, subtopic),
    dataTransfer(fileValidCb)
  {}

  /// @brief Destructor of the object.
  ~MqttCommon() override = default;

  /// @brief Initializes the MQTT common functionalities.
  /// @return `true` if initialization is successful, `false` otherwise.
  bool init() override {
    char versionString[dataOutBufSize] = {'\0'};
    const int32_t versionStringSize = snprintf_P(versionString, sizeof(versionString), versionMessageFrame,
    Build::getCppVersion(), Build::getFwVersion(), Build::getGitHash(), Build::getGitDirty(), ResetHandler::getResetReason());
    const bool versionStringValid = (versionStringSize >= 0 && versionStringSize < static_cast<int32_t>(sizeof(versionString)));
    if(!versionStringValid) { return false; }
    return MqttBase::sendMessage(versionString);
  }

  /// @brief Executes periodic tasks for file transfer and command processing.
  /// @return `true` if the run cycle is executed successfully, `false` otherwise.
  bool run() override {
    // cppcheck-suppress knownConditionTrueFalse
    if(isFileCheckDone) {
      isFileCheckDone = false;
      const uint32_t errCode = dataTransfer.getErrorCode();
      sendResponse(isFileValid, errCode);
      // cppcheck-suppress knownConditionTrueFalse
      if(!isFileValid) {
        Logger::get().printf_P(PSTR("[COMMON] Stored file is not valid!\r\n  Code: %u\r\n"), errCode);
      } else {
        if(isRestartRequired) {
          ResetHandler::restartMCU();
        }
      }
    }
    dataTransfer.runValidityCheck();
    return true;
  }

  /// @brief Processes an incoming MQTT message in JSON format.
  /// @param payloadJson JSON document containing the received MQTT message.
  void messageArrivedCallback(JsonDocument& payloadJson) override {
    JsonVariant binIdJsonVar = payloadJson[F("binId")];
    JsonVariant fileNameJsonVar = payloadJson[F("name")];
    JsonVariant fileSizeJsonVar = payloadJson[F("fileSize")];
    JsonVariant fileMd5JsonVar = payloadJson[F("md5")];
    JsonVariant filePieceJsonVar = payloadJson[F("piece")];
    JsonVariant fileDataJsonVar = payloadJson[F("data")];
    JsonVariant cmdJsonVar = payloadJson[F("cmd")];

    // Check for a command message first, before any file transfer fields.
    if(cmdJsonVar.is<const char*>()) {
      dispatchCommand(cmdJsonVar.as<const char*>());
      return;
    }

    const bool binIdPresented = binIdJsonVar.is<const char*>();
    const bool fileNamePresented = fileNameJsonVar.is<const char*>();
    const bool fileSizePresented = fileSizeJsonVar.is<uint32_t>();
    const bool fileMd5Presented = fileMd5JsonVar.is<const char*>();
    const bool filePiecePresented = filePieceJsonVar.is<uint32_t>();
    const bool fileDataPresented = fileDataJsonVar.is<const char*>();

    if(fileNamePresented && fileSizePresented && fileMd5Presented) {
      if(binIdPresented) {
        const char* binId = binIdJsonVar.as<const char*>();
        if(strncmp_P(binId, Build::getPioEnv(), Build::getPioEnvLength()) != 0) {
          Logger::get().printf_P(PSTR("[COMMON] Wrong FW file ID: '%s' expected: '%s'\r\n"), binId, Build::getPioEnv());
          sendResponse(false);
          return;
        }
        isRestartRequired = true;
      } else {
        isRestartRequired = false;
      }
      const uint32_t fileSize = fileSizeJsonVar.as<uint32_t>();
      const char* fileMd5 = fileMd5JsonVar.as<const char*>();
      const char* fileName = fileNameJsonVar.as<const char*>();
      const bool transferBeginResult = dataTransfer.begin(fileSize, fileMd5, fileName);
      const uint32_t beginErrCode = dataTransfer.getErrorCode();
      sendResponse(transferBeginResult, beginErrCode);
      if(!transferBeginResult) {
        Logger::get().printf_P(PSTR("[COMMON] Can't begin file transfer: %s\r\n  Code: %u\r\n"), fileName, beginErrCode);
      }
    } else if(filePiecePresented && fileDataPresented) {
      const uint32_t filePieceNumber = filePieceJsonVar.as<uint32_t>();
      const char* filePieceB64 = fileDataJsonVar.as<const char*>();
      const bool storingResult = dataTransfer.storeBase64(filePieceNumber, filePieceB64);
      const uint32_t storingErrCode = dataTransfer.getErrorCode();
      sendResponse(storingResult, storingErrCode);
      if(!storingResult) {
        Logger::get().printf_P(PSTR("[COMMON] File storing failed!\r\n  Code: %u\r\n"), storingErrCode);
      }
    } else {
      Logger::get().printf_P(PSTR("[COMMON] Unknown JSON file!\r\n"));
    }
  }

  /// @brief Registers a dynamic command with a handler function.
  /// @param name The command name string (stored in PROGMEM or RAM).
  /// @param handler Function pointer invoked when the command is received.
  void registerCommand(const char* name, void (*handler)()) {
    if constexpr (MaxDynCmds > 0) {
      if(name == nullptr || handler == nullptr || dynCmdCount >= MaxDynCmds) { return; }
      strncpy(dynCmdTable[dynCmdCount].name, name, maxCmdLength);
      dynCmdTable[dynCmdCount].name[maxCmdLength] = '\0';
      dynCmdTable[dynCmdCount].handler = handler;
      ++dynCmdCount;
    }
  }

  MqttCommon(const MqttCommon&) = delete;                       // Define copy constructor.
  MqttCommon& operator=(const MqttCommon&) = delete;            // Define copy assignment operator.
  MqttCommon(MqttCommon&&) = delete;                            // Define move constructor.
  MqttCommon& operator=(MqttCommon&&) = delete;                 // Define move assignment operator.

private:
  /// @brief Callback invoked after a file validity check is completed.
  /// @param isValid `true` if the file is valid, `false` otherwise.
  static void fileValidCb(bool isValid) {
    isFileCheckDone = true;
    isFileValid = isValid;
  }

  /// @brief Sends a response message over MQTT.
  /// @param result Outcome of the previous operation (`true` for success, `false` for failure).
  /// @param errCode Optional error code included in the response on failure (0 = no error).
  /// @return `true` if the response is sent successfully, `false` otherwise.
  bool sendResponse(bool result, uint32_t errCode = 0U) {
    const bool sendingResult = MqttBase::sendResponse((result ? MqttBase::Response::ACK : MqttBase::Response::NACK), 0U, errCode);
    if(!sendingResult) {
      Logger::get().printf_P(PSTR("[COMMON] Failed to send response '%hu'\r\n"), static_cast<uint8_t>(result));
    }
    return sendingResult;
  }

  /// @brief Dispatches an incoming command string to the appropriate handler.
  /// @param cmd Null-terminated command string received via MQTT.
  void dispatchCommand(const char* cmd) {
    static const struct CmdEntry { const char* name; void (MqttCommon::*handler)(); } cmdTable[] = {
      { cmdReboot, &MqttCommon::handleReboot },
    };
    for(const CmdEntry& entry : cmdTable) {
      // cppcheck-suppress useStlAlgorithm
      if(strncmp_P(cmd, entry.name, maxCmdLength) == 0) {
        (this->*entry.handler)();
        return;
      }
    }
    if constexpr (MaxDynCmds > 0) {
      for(uint8_t i = 0U; i < dynCmdCount; ++i) {
        // cppcheck-suppress useStlAlgorithm
        if(strncmp(cmd, dynCmdTable[i].name, maxCmdLength) == 0) {
          dynCmdTable[i].handler();
          sendResponse(true);
          return;
        }
      }
    }
    Logger::get().printf_P(PSTR("[COMMON] Unknown cmd: '%s'\r\n"), cmd);
    sendResponse(false);
  }

  /// @brief Handles the reboot command by restarting the MCU.
  void handleReboot() {
    Logger::get().printf_P(PSTR("[COMMON] Reboot command received.\r\n"));
    sendResponse(true);
    ResetHandler::restartMCU();
  }

  // --- Command dispatch table types ---

  /// @brief Entry in the dynamic command lookup table.
  struct DynCmdEntry {
    char name[maxCmdLength + 1U];                              // Command name string.
    void (*handler)();                                         // Function pointer to the command handler.
  };

  std::array<DynCmdEntry, MaxDynCmds> dynCmdTable{};          // Dynamic command lookup table.
  uint8_t dynCmdCount = 0U;                                   // Number of registered dynamic commands.

  static inline bool isFileCheckDone = false;       // Flag indicating that a file validity check is completed.
  static inline bool isFileValid = false;           // Flag indicating the result of the file validity check.

  DataTransfer dataTransfer;                        // Handles file transfer operations.
  bool isRestartRequired = false;                   // Flag indicating whether a device restart is required after file transfer.
};
