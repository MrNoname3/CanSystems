#ifndef SPIFLASH_H
#define SPIFLASH_H

#include <Arduino.h>                                                /// Arduino framework header.
#include <SPI.h>                                                    /// Arduino SPI library.
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Driver for read/write access to SPI flash memory chips (256-byte page, NAND-type).
/// @note NAND flash requires erase before write: cells can only transition from 1→0;
///       only the erase commands reset all bits back to 1. Smallest erasable unit: 4 KB sector.
/// @note The WP (write-protect) pin must be pulled HIGH to enable write/erase operations.
///       The WEL (Write Enable Latch) bit is set automatically before each write/erase command
///       and resets automatically on power-up, reset, or after any write/erase completes.
class SPIFlash final {
public:
  /// @brief Constructs a SPIFlash object.
  /// @param slaveSelectPin SPI chip-select pin.
  /// @param jedecID Expected JEDEC manufacturer/device ID; pass `0` to skip verification.
  ///                Example IDs: Atmel-Adesto AT25DF041A = 0x1F44, Winbond W25X40CL = 0xEF30.
  SPIFlash(uint8_t slaveSelectPin, uint16_t jedecID = 0U);

  /// @brief Initialises SPI, wakes the chip, and verifies the JEDEC ID if set.
  /// @return `true` if initialisation succeeded, `false` otherwise.
  [[nodiscard]] bool initialize();

  /// @brief Reads the flash status register.
  /// @return Raw status register value.
  [[nodiscard]] uint8_t readStatus();

  /// @brief Reads one byte from flash memory.
  /// @param addr 24-bit flash address.
  /// @return Byte value at the given address.
  [[nodiscard]] uint8_t readByte(uint32_t addr);

  /// @brief Reads multiple bytes from flash memory into a buffer.
  /// @param addr 24-bit start address.
  /// @param buf Destination buffer.
  /// @param len Number of bytes to read.
  void readBytes(uint32_t addr, void* buf, uint16_t len);

  /// @brief Writes one byte to flash memory.
  /// @param addr 24-bit flash address (must be pre-erased, i.e. all bits 1).
  /// @param byt Byte value to write.
  void writeByte(uint32_t addr, uint8_t byt);

  /// @brief Writes multiple bytes to flash memory, handling page boundaries automatically.
  /// @param addr 24-bit start address (must be pre-erased, i.e. all bits 1).
  /// @param buf Source buffer.
  /// @param len Number of bytes to write (up to 64 K).
  void writeBytes(uint32_t addr, const void* buf, uint16_t len);

  /// @brief Checks whether the chip is busy with a write or erase operation.
  /// @return `true` if the chip is busy, `false` otherwise.
  [[nodiscard]] bool busy();

  /// @brief Erases the entire flash chip (non-blocking).
  /// @note May take several seconds; poll busy() to wait for completion.
  ///       Any subsequent command will also wait for the chip automatically.
  void chipErase();

  /// @brief Erases a 4 KB block at the given address.
  /// @param addr Address within the target block.
  void blockErase4K(uint32_t addr);

  /// @brief Erases a 32 KB block at the given address.
  /// @param addr Address within the target block.
  void blockErase32K(uint32_t addr);

  /// @brief Erases a 64 KB block at the given address.
  /// @param addr Address within the target block.
  void blockErase64K(uint32_t addr);

  /// @brief Reads the JEDEC manufacturer and device ID.
  /// @return 16-bit JEDEC ID.
  [[nodiscard]] uint16_t readDeviceId();

  /// @brief Reads the 64-bit unique device ID into a caller-provided buffer.
  /// @note Only needs to be called once after initialize().
  /// @param buf 8-byte buffer to store the unique ID.
  void readUniqueId(uint8_t (&buf)[8]);

  /// @brief Puts the chip into deep power-down mode.
  void sleep();

  /// @brief Wakes the chip from deep power-down mode.
  void wakeup();

  /// @brief Ends the SPI bus.
  void end();

  SPIFlash(const SPIFlash&) = delete;
  SPIFlash& operator=(const SPIFlash&) = delete;
  SPIFlash(SPIFlash&&) = delete;
  SPIFlash& operator=(SPIFlash&&) = delete;

private:
  // clang-format off
  static constexpr uint8_t CMD_WRITE_ENABLE  = 0x06U;              // Write enable (WREN).
  static constexpr uint8_t CMD_ERASE_4K      = 0x20U;              // Erase one 4 KB block.
  static constexpr uint8_t CMD_ERASE_32K     = 0x52U;              // Erase one 32 KB block.
  static constexpr uint8_t CMD_ERASE_64K     = 0xD8U;              // Erase one 64 KB block.
  static constexpr uint8_t CMD_ERASE_CHIP    = 0x60U;              // Erase entire chip.
  static constexpr uint8_t CMD_STATUS_READ   = 0x05U;              // Read status register.
  static constexpr uint8_t CMD_STATUS_WRITE  = 0x01U;              // Write status register.
  static constexpr uint8_t CMD_ARRAY_READ    = 0x0BU;              // Read array fast (requires 1 dummy byte after address).
  static constexpr uint8_t CMD_ARRAY_READ_LF = 0x03U;              // Read array (low frequency).
  static constexpr uint8_t CMD_SLEEP         = 0xB9U;              // Deep power-down.
  static constexpr uint8_t CMD_WAKE          = 0xABU;              // Deep power-up.
  static constexpr uint8_t CMD_BYTE_PROGRAM  = 0x02U;              // Byte/page program (1–256 bytes).
  static constexpr uint8_t CMD_READ_ID       = 0x9FU;              // Read JEDEC manufacturer and device ID.
  static constexpr uint8_t CMD_READ_MAC      = 0x4BU;              // Read unique ID (MAC).
  // clang-format on

  /// @brief Asserts chip-select and saves SPI state.
  void select(); // NOLINT(readability-make-member-function-const)

  /// @brief De-asserts chip-select and restores SPI state.
  void unselect(); // NOLINT(readability-make-member-function-const)

  /// @brief Sends a command byte; issues WREN automatically for write/erase commands.
  /// @param cmd Command byte.
  /// @param isWrite Set to `true` for write/erase commands.
  void command(uint8_t cmd, bool isWrite = false);

  uint8_t slaveSelectPin;                                           // SPI chip-select pin.
  uint16_t jedecID;                                                 // Expected JEDEC device ID (0 = skip check).
  uint8_t spcr;                                                     // Saved SPCR register value.
  uint8_t spsr;                                                     // Saved SPSR register value.
#ifdef SPI_HAS_TRANSACTION
  SPISettings settings;                                             // SPI transaction settings.
#endif
};

#endif // SPIFLASH_H
