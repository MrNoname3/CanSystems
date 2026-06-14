#pragma once
// Native-test shim for the Arduino MD5Builder, with a real RFC 1321 MD5 so dataTransfer's
// integrity check (and the Update shim's firmware verification) are genuinely exercised on the
// host. API mirrors the Arduino class: begin() / add() / calculate() / toString() (lowercase hex).
#include <stdint.h>
#include <cstddef>
#include <string>

class MD5Builder {
public:
  void begin() {
    state_[0] = 0x67452301U;
    state_[1] = 0xefcdab89U;
    state_[2] = 0x98badcfeU;
    state_[3] = 0x10325476U;
    bitLen_ = 0U;
    bufLen_ = 0U;
  }

  void add(const uint8_t* data, uint16_t len) {
    for(uint16_t i = 0U; i < len; ++i) { pushByte(data[i]); }
    bitLen_ += static_cast<uint64_t>(len) * 8U;
  }

  void calculate() {
    const uint64_t lenBits = bitLen_;
    pushByte(0x80U);
    while(bufLen_ != 56U) { pushByte(0x00U); }
    for(uint8_t i = 0U; i < 8U; ++i) { pushByte(static_cast<uint8_t>((lenBits >> (8U * i)) & 0xFFU)); }
    for(uint8_t i = 0U; i < 4U; ++i) {
      for(uint8_t j = 0U; j < 4U; ++j) {
        digest_[(i * 4U) + j] = static_cast<uint8_t>((state_[i] >> (8U * j)) & 0xFFU);
      }
    }
  }

  [[nodiscard]] std::string toString() const {
    static const char* const hex = "0123456789abcdef";
    std::string out;
    out.reserve(32U);
    for(const uint8_t byte : digest_) {
      out += hex[byte >> 4U];
      out += hex[byte & 0x0FU];
    }
    return out;
  }

private:
  // Appends one byte to the working block (without touching the message length counter) and
  // runs the compression function whenever a full 64-byte block has accumulated.
  void pushByte(uint8_t byte) {
    buf_[bufLen_++] = byte;
    if(bufLen_ == 64U) {
      transform();
      bufLen_ = 0U;
    }
  }

  static uint32_t rotl(uint32_t value, uint8_t count) {
    return (value << count) | (value >> (32U - count));
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity) standard 64-step MD5 compression
  void transform() {
    static const uint32_t K[64] = {
      0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
      0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU, 0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
      0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
      0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
      0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
      0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
      0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
      0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U, 0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U
    };
    static const uint8_t S[64] = {
      7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
      5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U,
      4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
      6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U
    };

    uint32_t M[16];
    for(size_t i = 0U; i < 16U; ++i) {
      const size_t off = i * 4U;
      M[i] = static_cast<uint32_t>(buf_[off]) |
             (static_cast<uint32_t>(buf_[off + 1U]) << 8U) |
             (static_cast<uint32_t>(buf_[off + 2U]) << 16U) |
             (static_cast<uint32_t>(buf_[off + 3U]) << 24U);
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    for(uint8_t i = 0U; i < 64U; ++i) {
      uint32_t f = 0U;
      uint8_t g = 0U;
      if(i < 16U) {
        f = (b & c) | (~b & d);
        g = i;
      } else if(i < 32U) {
        f = (d & b) | (~d & c);
        g = static_cast<uint8_t>(((5U * i) + 1U) % 16U);
      } else if(i < 48U) {
        f = b ^ c ^ d;
        g = static_cast<uint8_t>(((3U * i) + 5U) % 16U);
      } else {
        f = c ^ (b | ~d);
        g = static_cast<uint8_t>((7U * i) % 16U);
      }
      f = f + a + K[i] + M[g];
      a = d;
      d = c;
      c = b;
      b = b + rotl(f, S[i]);
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
  }

  uint32_t state_[4] = { 0U, 0U, 0U, 0U };
  uint64_t bitLen_ = 0U;
  uint8_t buf_[64] = { 0U };
  uint8_t bufLen_ = 0U;
  uint8_t digest_[16] = { 0U };
};
