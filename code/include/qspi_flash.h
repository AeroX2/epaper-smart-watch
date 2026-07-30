#pragma once

#include <Arduino.h>

struct FlashIdentity {
  uint8_t manufacturer;
  uint8_t memory_type;
  uint8_t capacity;
};

class QspiFlash {
 public:
  bool begin();
  bool readIdentity(FlashIdentity& identity);
  bool ready() const;

 private:
  bool initialized_ = false;
};
