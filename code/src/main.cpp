#include "watch_application.h"

namespace {
WatchApplication application;
}

void setup() {
  application.setup();
}

void loop() {
  application.loop();
}
