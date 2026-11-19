#pragma once

#include <Arduino.h>

#include "watch_config.h"

class BLEDevice;
class BLECharacteristic;

// Watch side of the phone link, protocol v2 (see PROTOCOL.md at the repo
// root). Phone -> watch writes are parsed inside BLE.poll(), acknowledged, and
// queued for the application; watch -> phone notifications are queued and
// drained while the phone is subscribed, split into notifications of at most
// 20 bytes ('+' / '=' fragments). Everything is fixed-size: no heap use.

enum class PhoneUpdateType : uint8_t {
  None,
  Hello,           // number[0]=proto
  Time,            // utc, number[0]=offset seconds, number[2]=next offset
                   // change (utc, 0 = none), number[1]=offset after it
  Notify,          // number[0]=id, number[1]=alert (1 = new arrival),
                   // text_a=app, text_b=title, text_c=text
  NotifyClear,     // number[0]=id (0 = all)
  Call,            // number[0]=1 ring / 0 end, text_a=caller
  Weather,         // number[0..3]=temp, high, low, code; text_a=condition
  Music,           // number[0..2]=state, position, duration;
                   // text_a=title, text_b=artist
  NextAlarm,       // utc (0 = none)
  Event,           // utc (0 = none), text_a=title
  Phone,           // number[0..2]=battery, charging, dnd
  Settings,        // number[0..1]=vibration, quiet; number[2]=tone 0-5 or
                   // -1 when the field is missing (unchanged)
  TonePreview,     // number[0]=tone 1-5
  FindWatch,
  RequestStatus,
  RequestHistory,
  StepsReset,
  SleepStart,
  SleepStop,
  SleepPause,      // number[0]=seconds
  SleepAlarm,      // number[0]=delay seconds
  SleepAlarmStop,
  SleepNotify,     // text_a=title, text_b=text
  SleepHint,       // number[0]=repeats
  Camera,          // number[0]=CameraState, number[1]=countdown seconds or
                   // photos saved; text_a=error detail
  OtaBegin,        // number[0]=size, utc=crc32, text_a=version
  OtaEnd,
  OtaAbort,
};

// HELLO reply flags (PROTOCOL.md).
constexpr uint8_t HELLO_FLAG_SECURE = 1;
constexpr uint8_t HELLO_FLAG_OTA = 2;
constexpr uint8_t HELLO_FLAG_STORAGE = 4;

struct PhoneUpdate {
  PhoneUpdateType type = PhoneUpdateType::None;
  uint32_t utc = 0;
  int32_t number[4]{};
  char text_a[29]{};
  char text_b[29]{};
  char text_c[57]{};
};

class WatchBle {
 public:
  static constexpr size_t MESSAGE_MAX = 160;
  static constexpr uint8_t INBOUND_CAPACITY = 8;
  static constexpr uint8_t OUTBOUND_CAPACITY = 16;
  // Watch -> phone notifications never exceed this (PROTOCOL.md).
  static constexpr size_t NOTIFICATION_MAX = 20;
  // offset (4) + up to 240 data bytes per OTA data write.
  static constexpr size_t OTA_WRITE_MAX = 244;

  using OtaDataHandler = void (*)(const uint8_t* data, size_t length);

  // secure: require an encrypted link from the bonded phone (see the
  // "Link encryption" notes in watch_ble.cpp). hello_flags is reported in
  // the HELLO reply.
  bool begin(Print& out, bool secure, uint8_t hello_flags);
  void end(Print& out);

  // Drains controller events (parsing any phone writes) and sends queued
  // notifications. Call after every wake. Also safe to call from the display
  // wait hook: it only receives, acknowledges and queues, and it returns
  // immediately instead of re-entering BLE.poll() or the send loop.
  void poll();
  // Sends queued notifications without polling the controller.
  void flush();

  // Called from inside BLE.poll() for every accepted write to the OTA data
  // characteristic.
  void setOtaDataHandler(OtaDataHandler handler);

  // Bond key for secure mode. setBond() loads the stored key before begin();
  // takeBondChange() is true once after a new phone has paired, so the
  // application can persist bond().
  void setBond(const StoredBond& bond);
  const StoredBond& bond() const;
  bool takeBondChange();
  bool secure() const;
  // Link currently encrypted (always false when not connected).
  bool encrypted() const;

  bool takeUpdate(PhoneUpdate& update);

  // Queues one watch -> phone message. Fails when no phone is connected or
  // the outbound queue is full. Longer messages are truncated to 160 bytes.
  bool send(const char* message);
  bool sendf(const char* format, ...) __attribute__((format(printf, 2, 3)));
  uint8_t outboundFree() const;
  bool outboundPending() const;
  // A queued message could go out right now (connected and subscribed).
  bool sendPending() const;

  // Power policy helpers. rxPending(): the controller has delivered events
  // that poll() has not drained yet. transportBusy(): an outgoing ACL packet
  // has not been taken by CPU2 yet. Both only read flags, so they are safe
  // with interrupts disabled.
  bool rxPending() const;
  bool transportBusy() const;

  // Advertising interval policy: fast (100 ms) for ADV_FAST_WINDOW_MS after
  // the radio starts or a phone disconnects, about 1 s otherwise. Call from
  // the main loop only (it sends HCI commands).
  void serviceAdvertising();
  // Milliseconds until serviceAdvertising() has something to do, or
  // UINT32_MAX.
  uint32_t msUntilAdvertisingService() const;
  bool advertisingFast() const { return adv_fast_; }

  void updateBattery(uint8_t percent);

  bool ready() const;
  bool connected() const;
  // Writes received from the phone and notifications sent, since boot.
  uint32_t rxWrites() const { return rx_writes_; }
  uint32_t txNotifications() const { return tx_notifications_; }
  // True once after the link drops (outbound queue already cleared).
  bool takeDisconnected();

 private:
  enum class ParseResult : uint8_t { Ok, Parse, Unknown };

  struct OutboundMessage {
    uint8_t length;
    char data[MESSAGE_MAX];
  };

  static void onPhoneWrite(BLEDevice device, BLECharacteristic characteristic);
  static void onOtaWrite(BLEDevice device, BLECharacteristic characteristic);
  static int onGetLtk(uint8_t* address, uint8_t* ltk);
  static int onStoreLtk(uint8_t* address, uint8_t* ltk);
  // Connected, and encrypted when secure mode demands it.
  bool linkUsable() const;
  void refuseUnencrypted();
  void handleWrite(const uint8_t* data, int length);
  ParseResult parse(char* const* fields, uint8_t count, PhoneUpdate& update);
  void enqueueControl(const char* format, ...)
      __attribute__((format(printf, 2, 3)));
  void clearOutbound();
  void drainOutbound();
  void applyAdvertising(bool fast);

  static constexpr uint32_t ADV_FAST_WINDOW_MS = 30000;
  static constexpr uint32_t ADV_RETRY_MS = 2000;
  // Units of 0.625 ms: 100 ms and 1022.5 ms.
  static constexpr uint16_t ADV_INTERVAL_FAST = 160;
  static constexpr uint16_t ADV_INTERVAL_SLOW = 1636;

  uint32_t rx_writes_ = 0;
  uint32_t tx_notifications_ = 0;
  bool adv_fast_ = true;            // interval currently programmed
  bool adv_boost_pending_ = false;  // a disconnect asks for a fast window
  bool adv_retry_ = false;          // the last change was refused
  bool adv_retry_fast_ = false;
  uint32_t adv_mark_ms_ = 0;        // start of the fast window / last attempt

  bool ready_ = false;
  bool connected_ = false;
  bool busy_ = false;  // inside poll()/flush(): no nested entry
  bool secure_ = false;
  uint8_t hello_flags_ = 0;
  uint32_t last_refusal_ms_ = 0;
  bool bond_changed_ = false;
  StoredBond bond_{};
  OtaDataHandler ota_handler_ = nullptr;
  bool disconnected_event_ = false;
  uint8_t last_battery_ = 0xFF;
  uint16_t last_seq_ = 0;  // 0 = no accepted message since HELLO/connect

  PhoneUpdate inbound_[INBOUND_CAPACITY]{};
  uint8_t inbound_head_ = 0;
  uint8_t inbound_count_ = 0;

  OutboundMessage outbound_[OUTBOUND_CAPACITY]{};
  uint8_t outbound_head_ = 0;
  uint8_t outbound_count_ = 0;

  // Message currently being sent (possibly as several fragments).
  OutboundMessage sending_{};
  bool sending_active_ = false;
  uint8_t sending_offset_ = 0;
};
