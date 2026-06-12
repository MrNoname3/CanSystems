#pragma once
// Native-test shim for the Arduino SoftwareSerial library. DFPlayer only needs construction,
// begin(), and the Stream interface that DFPlayerMiniFast writes its packets through — the
// Stream shim captures every written byte for packet-level assertions.
#include <stdint.h>
#include "Stream.h"

class SoftwareSerial : public Stream {
public:
  SoftwareSerial(uint8_t /*rxPin*/, uint8_t /*txPin*/) {}
  void begin(long /*baud*/) {}                       // NOLINT(readability-convert-member-functions-to-static)
};
