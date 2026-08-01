#include "watch_ble.h"

#include <STM32duinoBLE.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


namespace {

constexpr char WATCH_SERVICE_UUID[] = "7BD10000-6B10-4A21-9D6A-3A56B35C1000";
constexpr char PHONE_TO_WATCH_UUID[] =
    "7BD10001-6B10-4A21-9D6A-3A56B35C1000";
constexpr char WATCH_TO_PHONE_UUID[] =
    "7BD10002-6B10-4A21-9D6A-3A56B35C1000";

BLEService watch_service(WATCH_SERVICE_UUID);
BLECharacteristic phone_to_watch(PHONE_TO_WATCH_UUID,
                                  BLEWrite | BLEWriteWithoutResponse, 160);
BLECharacteristic watch_to_phone(WATCH_TO_PHONE_UUID, BLERead | BLENotify, 80);
BLEService battery_service("180F");
BLEUnsignedCharCharacteristic battery_level("2A19", BLERead | BLENotify);

template <size_t Size>
void copyText(char (&destination)[Size], const char* source) {
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  strncpy(destination, source, Size - 1);
  destination[Size - 1] = '\0';
}

char* splitField(char** cursor) {
  if (cursor == nullptr || *cursor == nullptr) {
    return nullptr;
  }
  char* start = *cursor;
  char* separator = strchr(start, '|');
  if (separator != nullptr) {
    *separator = '\0';
    *cursor = separator + 1;
  } else {
    *cursor = nullptr;
  }
  return start;
}

}  // namespace

bool WatchBle::begin(Print& out) {
  if (ready_) {
    return true;
  }
  out.println(F("Starting STM32WB Bluetooth LE radio..."));
  if (!BLE.begin()) {
    out.println(F("[FAIL] BLE startup failed."));
    out.println(F(
        "Install the matching stm32wbxx_BLE_HCILayer_fw.bin on CPU2/FUS."));
    return false;
  }

  BLE.setLocalName("E-Paper Watch");
  BLE.setDeviceName("E-Paper Watch");
  BLE.setAdvertisedService(watch_service);

  watch_service.addCharacteristic(phone_to_watch);
  watch_service.addCharacteristic(watch_to_phone);
  battery_service.addCharacteristic(battery_level);
  BLE.addService(watch_service);
  BLE.addService(battery_service);

  static constexpr char INITIAL_STATUS[] = "READY,1";
  watch_to_phone.writeValue(
      reinterpret_cast<const uint8_t*>(INITIAL_STATUS),
      sizeof(INITIAL_STATUS) - 1);
  battery_level.writeValue(static_cast<uint8_t>(0));

  BLE.advertise();
  ready_ = true;
  out.println(F("[PASS] BLE advertising as \"E-Paper Watch\""));
  out.print(F("Custom service: "));
  out.println(WATCH_SERVICE_UUID);
  return true;
}

void WatchBle::end(Print& out) {
  if (!ready_) {
    return;
  }
  BLE.stopAdvertise();
  if (BLE.connected()) {
    BLE.disconnect();
  }
  BLE.end();
  ready_ = false;
  connected_ = false;
  out.println(F("BLE stopped."));
}

void WatchBle::poll() {
  if (!ready_) {
    connected_ = false;
    return;
  }
  BLE.poll();
  connected_ = BLE.connected();
  if (!phone_to_watch.written()) {
    return;
  }

  char message[161]{};
  const int length =
      phone_to_watch.readValue(reinterpret_cast<uint8_t*>(message), 160);
  if (length <= 0) {
    return;
  }
  message[length < 160 ? length : 160] = '\0';
  parseMessage(message);
}

bool WatchBle::takeUpdate(PhoneUpdate& update) {
  if (!update_pending_) {
    return false;
  }
  update = pending_;
  pending_ = {};
  update_pending_ = false;
  return true;
}

bool WatchBle::sendCommand(const char* command) {
  if (!ready_ || !connected_ || command == nullptr) {
    return false;
  }
  const size_t length = strnlen(command, 80);
  return watch_to_phone.writeValue(
             reinterpret_cast<const uint8_t*>(command),
             static_cast<int>(length)) != 0;
}

void WatchBle::updateBattery(uint8_t percent) {
  if (!ready_ || percent == last_battery_) {
    return;
  }
  last_battery_ = percent;
  battery_level.writeValue(percent);
}

bool WatchBle::ready() const {
  return ready_;
}

bool WatchBle::connected() const {
  return connected_;
}

void WatchBle::parseMessage(char* message) {
  pending_ = {};
  if (strncmp(message, "TIME,", 5) == 0) {
    const unsigned long epoch_value = strtoul(message + 5, nullptr, 10);
    if (epoch_value >= 946684800UL) {
      pending_.type = PhoneUpdateType::Time;
      pending_.epoch = static_cast<time_t>(epoch_value);
    }
  } else if (strncmp(message, "NOTIFY,", 7) == 0) {
    char* cursor = message + 7;
    copyText(pending_.sender, splitField(&cursor));
    copyText(pending_.line_1, splitField(&cursor));
    copyText(pending_.line_2, splitField(&cursor));
    pending_.type = PhoneUpdateType::Notification;
  } else if (strcmp(message, "CLEAR_NOTIFICATION") == 0) {
    pending_.type = PhoneUpdateType::ClearNotification;
  } else if (strncmp(message, "WEATHER,", 8) == 0) {
    char* cursor = message + 8;
    char* condition = strchr(cursor, '|');
    if (condition != nullptr) {
      *condition++ = '\0';
      int temperature = 0;
      int high = 0;
      int low = 0;
      if (sscanf(cursor, "%d,%d,%d", &temperature, &high, &low) == 3) {
        pending_.temperature_c = static_cast<int16_t>(temperature);
        pending_.high_c = static_cast<int8_t>(high);
        pending_.low_c = static_cast<int8_t>(low);
        copyText(pending_.condition, condition);
        pending_.type = PhoneUpdateType::Weather;
      }
    }
  } else if (strncmp(message, "MUSIC,", 6) == 0) {
    char* cursor = message + 6;
    char* metadata = strchr(cursor, '|');
    if (metadata != nullptr) {
      *metadata++ = '\0';
      int playing = 0;
      unsigned int position = 0;
      unsigned int duration = 0;
      if (sscanf(cursor, "%d,%u,%u", &playing, &position, &duration) == 3) {
        pending_.playing = playing != 0;
        pending_.position_seconds =
            static_cast<uint16_t>(min(position, 65535U));
        pending_.duration_seconds =
            static_cast<uint16_t>(min(duration, 65535U));
        char* fields = metadata;
        copyText(pending_.title, splitField(&fields));
        copyText(pending_.artist, splitField(&fields));
        pending_.type = PhoneUpdateType::Music;
      }
    }
  } else if (strcmp(message, "STEPS,RESET") == 0) {
    pending_.type = PhoneUpdateType::ResetSteps;
  } else if (strcmp(message, "SLEEP,START") == 0) {
    pending_.type = PhoneUpdateType::SleepStart;
  } else if (strcmp(message, "SLEEP,STOP") == 0) {
    pending_.type = PhoneUpdateType::SleepStop;
  } else if (strncmp(message, "SLEEP,PAUSE,", 12) == 0) {
    pending_.value = strtoul(message + 12, nullptr, 10);
    pending_.type = PhoneUpdateType::SleepPause;
  } else if (strncmp(message, "SLEEP,ALARM,", 12) == 0) {
    pending_.value = strtoul(message + 12, nullptr, 10);
    pending_.type = PhoneUpdateType::SleepAlarmStart;
  } else if (strcmp(message, "SLEEP,ALARM_STOP") == 0) {
    pending_.type = PhoneUpdateType::SleepAlarmStop;
  } else if (strncmp(message, "SLEEP,NOTIFY,", 13) == 0) {
    char* cursor = message + 13;
    copyText(pending_.line_1, splitField(&cursor));
    copyText(pending_.line_2, splitField(&cursor));
    pending_.type = PhoneUpdateType::SleepNotification;
  } else if (strncmp(message, "SLEEP,HINT,", 11) == 0) {
    pending_.value = strtoul(message + 11, nullptr, 10);
    pending_.type = PhoneUpdateType::SleepHint;
  }
  update_pending_ = pending_.type != PhoneUpdateType::None;
}
