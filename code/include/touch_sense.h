#pragma once

#include <Arduino.h>

struct TouchReadings {
  uint16_t channel_1;
  uint16_t channel_2;
  uint16_t channel_3;
};

class TouchSense {
 public:
  bool begin();
  bool read(TouchReadings& readings);
  bool ready() const;

 private:
  bool acquire(uint32_t channel, uint16_t& value);
  bool initialized_ = false;
};
