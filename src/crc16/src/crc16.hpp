#ifndef CRC16_HPP
#define CRC16_HPP

#include <stdint.h>

class Crc16 final {
  public:
    explicit Crc16(uint16_t initValue = 0, uint16_t polynomial = 0x1021);
    ~Crc16() = default;
    void next(uint8_t value);
    void next(const uint8_t* values, uint32_t length);
    uint16_t get() const;
    static uint16_t calculate(const uint8_t *data, uint32_t length);

    Crc16(const Crc16&) = delete;                       // Define copy constructor.
    Crc16& operator=(const Crc16&) = delete;            // Define copy assignment operator.
    Crc16(Crc16&&) = delete;                            // Define move constructor.
    Crc16& operator=(Crc16&&) = delete;                 // Define move assignment operator.
  private:
    uint16_t crc_;                                      // CRC16 starting value.
    const uint16_t polynomial_;                         // CRC16 polynomial.
  };
#endif // CRC16_HPP