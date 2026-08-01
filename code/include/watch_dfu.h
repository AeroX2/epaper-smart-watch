#pragma once

#include <Arduino.h>

class WatchDfu {
 public:
  bool available() const;
  void request(Print& out) const;
};
