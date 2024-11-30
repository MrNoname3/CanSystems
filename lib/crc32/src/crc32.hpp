#ifndef CRC32_HPP
#define CRC32_HPP

#include <stdint.h>

class Crc32 final {
  public:
    explicit Crc32(uint32_t initValue = 0xFFFFFFFF, uint32_t polynomial = 0xEDB88320);
    ~Crc32() = default;
    void next(uint8_t value);
    void next(const uint8_t* values, uint32_t length);
    uint32_t get() const;
    void reset();
    static uint32_t calculate(const uint8_t *data, uint32_t length);

    Crc32(const Crc32&) = delete;                       // Define copy constructor.
    Crc32& operator=(const Crc32&) = delete;            // Define copy assignment operator.
    Crc32(Crc32&&) = delete;                            // Define move constructor.
    Crc32& operator=(Crc32&&) = delete;                 // Define move assignment operator.
  private:
    uint32_t crc_;                                      // CRC32 starting value.
    const uint32_t initValue_;                          // CRC initial value.
    const uint32_t polynomial_;                         // CRC32 polynomial.
  };
#endif // CRC32_HPP