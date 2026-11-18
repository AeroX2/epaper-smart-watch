#include "watch_ble.h"

#include <STM32duinoBLE.h>
// Library-internal header, used only for ATT.paired(): the public
// BLE.paired() polls the controller, which must not happen from inside a
// write handler that is itself running under BLE.poll().
#include <utility/ATT.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "watch_version.h"
#include "watch_watchdog.h"

// Receive FIFO indices and flags of STM32duinoBLE 1.4.2's shared-memory
// transport (utility/HCISharedMemTransport.cpp). They are read here, never
// written, to answer "is there controller traffic waiting?" without calling
// into the library, whose own available() re-enables interrupts.
extern volatile uint16_t _read_index;
extern volatile uint16_t _write_index;
extern volatile bool data_overflow;
extern volatile bool acl_data_on;

namespace {

constexpr char WATCH_SERVICE_UUID[] = "7BD10000-6B10-4A21-9D6A-3A56B35C1000";
constexpr char PHONE_TO_WATCH_UUID[] =
    "7BD10001-6B10-4A21-9D6A-3A56B35C1000";
constexpr char WATCH_TO_PHONE_UUID[] =
    "7BD10002-6B10-4A21-9D6A-3A56B35C1000";

constexpr char OTA_DATA_UUID[] = "7BD10003-6B10-4A21-9D6A-3A56B35C1000";

constexpr int PROTOCOL_VERSION = 2;
constexpr uint8_t MAX_FIELDS = 8;

BLEService watch_service(WATCH_SERVICE_UUID);
BLECharacteristic phone_to_watch(PHONE_TO_WATCH_UUID,
                                 BLEWrite | BLEWriteWithoutResponse,
                                 WatchBle::MESSAGE_MAX);
BLECharacteristic watch_to_phone(WATCH_TO_PHONE_UUID, BLERead | BLENotify,
                                 WatchBle::MESSAGE_MAX);
BLECharacteristic ota_data(OTA_DATA_UUID, BLEWrite, WatchBle::OTA_WRITE_MAX);
BLEService battery_service("180F");
BLEUnsignedCharCharacteristic battery_level("2A19", BLERead | BLENotify);

WatchBle* active_instance = nullptr;

}  // namespace

// STM32duinoBLE's default global uses a static *random* address that it
// regenerates from the RNG on every BLE.begin(), so the watch would appear as
// a brand-new device after each radio start and the phone's saved pairing
// (which is by address) would never reconnect. Use the fixed public address
// the transport derives from the chip's unique device number instead. The
// library's own definition is disabled with -D FAKE_BLELOCALDEVICE.
BLELocalDevice BLEObj(PUBLIC_ADDR);
BLELocalDevice& BLE = BLEObj;

namespace {

// Copies at most max_length characters (and never more than the destination
// holds). Input is already sanitised to printable ASCII.
template <size_t Size>
void copyField(char (&destination)[Size], const char* source,
               size_t max_length) {
  const size_t limit = max_length < Size - 1 ? max_length : Size - 1;
  size_t length = 0;
  if (source != nullptr) {
    while (length < limit && source[length] != '\0') {
      destination[length] = source[length];
      ++length;
    }
  }
  destination[length] = '\0';
}

// Decimal integer with optional leading '-'. An empty field reads as 0.
bool parseInteger(const char* field, int32_t minimum, int32_t maximum,
                  int32_t& value) {
  int64_t result = 0;
  bool negative = false;
  if (field != nullptr && *field != '\0') {
    if (*field == '-') {
      negative = true;
      ++field;
      if (*field == '\0') {
        return false;
      }
    }
    uint8_t digits = 0;
    while (*field != '\0') {
      if (*field < '0' || *field > '9' || ++digits > 10) {
        return false;
      }
      result = result * 10 + (*field++ - '0');
    }
  }
  if (negative) {
    result = -result;
  }
  if (result < minimum || result > maximum) {
    return false;
  }
  value = static_cast<int32_t>(result);
  return true;
}

bool parseUnsigned(const char* field, uint32_t& value) {
  uint64_t result = 0;
  if (field != nullptr) {
    uint8_t digits = 0;
    while (*field != '\0') {
      if (*field < '0' || *field > '9' || ++digits > 10) {
        return false;
      }
      result = result * 10 + static_cast<uint64_t>(*field++ - '0');
    }
  }
  if (result > UINT32_MAX) {
    return false;
  }
  value = static_cast<uint32_t>(result);
  return true;
}

bool parseFlag(const char* field, int32_t& value) {
  return parseInteger(field, 0, 1, value);
}

}  // namespace

// Link encryption ("SECURE LINK", off by default)
//
// STM32duinoBLE 1.4.2 implements LE Secure Connections "Just Works" pairing
// in the host and exposes the long-term key through setStoreLTK/setGetLTK.
// Its BLEEncryption characteristic permission is NOT used here: when an
// unencrypted peer writes to such a characteristic the library copies the
// whole write (up to 244 bytes here) into a 64-byte ATTClass::writeBuffer,
// and a held read response into a 64-byte holdBuffer, with no length check
// (utility/ATT.cpp). That is a pre-authentication memory overwrite on
// exactly the characteristics this protocol needs to protect.
//
// Instead the requirement is enforced one layer up, with the same effect on
// what an unencrypted peer can do:
//  - nothing written by a peer is parsed, applied or acknowledged, and no
//    OTA data is accepted, unless the link is encrypted;
//  - nothing but the "encryption required" HELLO is sent on an unencrypted
//    link (sendf() refuses);
//  - pairing is only accepted while no phone is bonded. Once a key is
//    stored, only a peer that can encrypt with that key gets through, and
//    onStoreLtk() ignores any further key (the library would otherwise call
//    it for an unauthenticated Identity Address packet).
// Consequence for the phone: it gets no ATT "insufficient encryption" error.
// It learns that bonding is required from HELLO flag 1, which the watch
// sends, unacknowledged, in answer to any unencrypted write.
bool WatchBle::begin(Print& out, bool secure, uint8_t hello_flags) {
  if (ready_) {
    return true;
  }
  secure_ = secure;
  hello_flags_ = hello_flags;
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

  if (secure_) {
    BLE.setStoreLTK(onStoreLtk);
    BLE.setGetLTK(onGetLtk);
    // 0 = reject pairing, 1 = accept. Only an unbonded watch pairs.
    BLE.setPairable(bond_.valid ? 0 : 1);
  } else {
    // Library defaults: exactly the behaviour without this feature.
    BLE.setStoreLTK(nullptr);
    BLE.setGetLTK(nullptr);
    BLE.setPairable(1);
  }

  watch_service.addCharacteristic(phone_to_watch);
  watch_service.addCharacteristic(watch_to_phone);
  watch_service.addCharacteristic(ota_data);
  battery_service.addCharacteristic(battery_level);
  BLE.addService(watch_service);
  BLE.addService(battery_service);

  active_instance = this;
  phone_to_watch.setEventHandler(BLEWritten, onPhoneWrite);
  ota_data.setEventHandler(BLEWritten, onOtaWrite);

  char initial[NOTIFICATION_MAX + 1];
  int length = snprintf(initial, sizeof(initial), "HELLO|%d|%s|%u",
                        PROTOCOL_VERSION, WATCH_FIRMWARE_VERSION,
                        static_cast<unsigned>(hello_flags_));
  if (length > static_cast<int>(NOTIFICATION_MAX)) {
    length = NOTIFICATION_MAX;
  }
  watch_to_phone.writeValue(reinterpret_cast<const uint8_t*>(initial),
                            length);
  battery_level.writeValue(static_cast<uint8_t>(0));

  BLE.setAdvertisingInterval(ADV_INTERVAL_FAST);
  BLE.advertise();
  adv_fast_ = true;
  adv_boost_pending_ = false;
  adv_retry_ = false;
  adv_mark_ms_ = millis();
  ready_ = true;
  connected_ = false;
  last_seq_ = 0;
  inbound_head_ = 0;
  inbound_count_ = 0;
  clearOutbound();
  out.println(F("[PASS] BLE advertising as \"E-Paper Watch\""));
  if (secure_) {
    out.println(bond_.valid
                    ? F("Secure link: only the bonded phone is accepted.")
                    : F("Secure link: waiting for a phone to pair."));
  }
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
  if (connected_) {
    disconnected_event_ = true;
  }
  connected_ = false;
  last_seq_ = 0;
  last_refusal_ms_ = 0;
  clearOutbound();
  out.println(F("BLE stopped."));
}

void WatchBle::poll() {
  if (!ready_) {
    connected_ = false;
    return;
  }
  if (busy_) {
    // Reached through the display wait hook while an outer poll()/flush()
    // is still running: BLE.poll() must not be re-entered.
    return;
  }
  busy_ = true;
  // Phone writes are handled by onPhoneWrite() from inside BLE.poll().
  BLE.poll();
  const bool now_connected = BLE.connected();
  if (connected_ && !now_connected) {
    clearOutbound();
    last_seq_ = 0;
    disconnected_event_ = true;
    // The library has already re-enabled advertising with the interval that
    // was last programmed; ask for a fast window so the phone finds us.
    adv_boost_pending_ = true;
  }
  connected_ = now_connected;
  drainOutbound();
  busy_ = false;
}

void WatchBle::flush() {
  if (ready_ && !busy_) {
    busy_ = true;
    drainOutbound();
    busy_ = false;
  }
}

void WatchBle::setOtaDataHandler(OtaDataHandler handler) {
  ota_handler_ = handler;
}

void WatchBle::setBond(const StoredBond& bond) {
  bond_ = bond;
  bond_changed_ = false;
  if (ready_ && secure_) {
    BLE.setPairable(bond_.valid ? 0 : 1);
  }
}

const StoredBond& WatchBle::bond() const {
  return bond_;
}

bool WatchBle::takeBondChange() {
  const bool changed = bond_changed_;
  bond_changed_ = false;
  return changed;
}

bool WatchBle::secure() const {
  return secure_;
}

bool WatchBle::encrypted() const {
  return ready_ && connected_ && ATT.paired();
}

bool WatchBle::linkUsable() const {
  return ready_ && connected_ && (!secure_ || ATT.paired());
}

int WatchBle::onGetLtk(uint8_t* /*address*/, uint8_t* ltk) {
  // One bond only, so the peer address (a resolvable private address that
  // changes anyway) is not matched: a peer without this key cannot complete
  // the encryption and the controller drops the link.
  WatchBle* self = active_instance;
  if (self == nullptr || !self->secure_ || !self->bond_.valid ||
      ltk == nullptr) {
    return 0;
  }
  memcpy(ltk, self->bond_.ltk, sizeof(self->bond_.ltk));
  return 1;
}

int WatchBle::onStoreLtk(uint8_t* address, uint8_t* ltk) {
  WatchBle* self = active_instance;
  // Accept a key only from a real pairing: no existing bond, and the link
  // already encrypted with the key being stored.
  if (self == nullptr || !self->secure_ || self->bond_.valid ||
      address == nullptr || ltk == nullptr || !ATT.paired()) {
    return 0;
  }
  memcpy(self->bond_.address, address, sizeof(self->bond_.address));
  memcpy(self->bond_.ltk, ltk, sizeof(self->bond_.ltk));
  self->bond_.valid = true;
  self->bond_changed_ = true;
  BLE.setPairable(0);  // a variable write only; no controller traffic
  return 1;
}

void WatchBle::refuseUnencrypted() {
  // Tell the phone why it is being ignored, but not for every write.
  const uint32_t now = millis();
  if (last_refusal_ms_ != 0 && now - last_refusal_ms_ < 1000) {
    return;
  }
  last_refusal_ms_ = now == 0 ? 1 : now;
  enqueueControl("HELLO|%d|%s|%u", PROTOCOL_VERSION, WATCH_FIRMWARE_VERSION,
                 static_cast<unsigned>(hello_flags_));
}

bool WatchBle::takeUpdate(PhoneUpdate& update) {
  if (inbound_count_ == 0) {
    return false;
  }
  update = inbound_[inbound_head_];
  inbound_head_ = static_cast<uint8_t>((inbound_head_ + 1) % INBOUND_CAPACITY);
  --inbound_count_;
  return true;
}

bool WatchBle::send(const char* message) {
  return message != nullptr && sendf("%s", message);
}

bool WatchBle::sendf(const char* format, ...) {
  if (!linkUsable() || format == nullptr ||
      outbound_count_ >= OUTBOUND_CAPACITY) {
    return false;
  }
  OutboundMessage& slot =
      outbound_[(outbound_head_ + outbound_count_) % OUTBOUND_CAPACITY];
  char buffer[MESSAGE_MAX + 1];
  va_list args;
  va_start(args, format);
  const int written = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (written < 0) {
    return false;
  }
  const size_t length =
      static_cast<size_t>(written) < MESSAGE_MAX
          ? static_cast<size_t>(written)
          : MESSAGE_MAX;
  memcpy(slot.data, buffer, length);
  slot.length = static_cast<uint8_t>(length);
  ++outbound_count_;
  return true;
}

uint8_t WatchBle::outboundFree() const {
  return static_cast<uint8_t>(OUTBOUND_CAPACITY - outbound_count_);
}

bool WatchBle::outboundPending() const {
  return ready_ && connected_ && outbound_count_ > 0;
}

bool WatchBle::sendPending() const {
  return ready_ && connected_ && (outbound_count_ > 0 || sending_active_) &&
         watch_to_phone.subscribed();
}

bool WatchBle::rxPending() const {
  return ready_ && (_read_index != _write_index || data_overflow);
}

bool WatchBle::transportBusy() const {
  return ready_ && acl_data_on;
}

void WatchBle::applyAdvertising(bool fast) {
  busy_ = true;  // no nested poll()/flush() while HCI commands are in flight
  BLE.stopAdvertise();
  BLE.setAdvertisingInterval(fast ? ADV_INTERVAL_FAST : ADV_INTERVAL_SLOW);
  bool ok = BLE.advertise() != 0;
  if (!ok) {
    ok = BLE.advertise() != 0;
  }
  busy_ = false;
  adv_mark_ms_ = millis();
  if (ok) {
    adv_fast_ = fast;
    adv_retry_ = false;
  } else {
    // Advertising may be off now: keep trying until it is back.
    adv_retry_ = true;
    adv_retry_fast_ = fast;
  }
}

void WatchBle::serviceAdvertising() {
  if (!ready_ || busy_) {
    return;
  }
  if (!adv_boost_pending_ && !adv_retry_ && !adv_fast_) {
    return;  // settled on the slow interval
  }
  // BLE.connected() drains the controller first, so a connection that has
  // just formed is seen before advertising is touched.
  if (connected_ || BLE.connected()) {
    return;
  }
  const uint32_t now = millis();
  if (adv_retry_) {
    if (now - adv_mark_ms_ >= ADV_RETRY_MS) {
      applyAdvertising(adv_retry_fast_);
    }
    return;
  }
  if (adv_boost_pending_) {
    adv_boost_pending_ = false;
    if (adv_fast_) {
      adv_mark_ms_ = now;  // already fast: just restart the window
    } else {
      applyAdvertising(true);
    }
    return;
  }
  if (now - adv_mark_ms_ >= ADV_FAST_WINDOW_MS) {
    applyAdvertising(false);
  }
}

uint32_t WatchBle::msUntilAdvertisingService() const {
  if (!ready_ || connected_) {
    return UINT32_MAX;
  }
  if (adv_boost_pending_) {
    return 0;
  }
  const uint32_t elapsed = millis() - adv_mark_ms_;
  if (adv_retry_) {
    return elapsed >= ADV_RETRY_MS ? 0 : ADV_RETRY_MS - elapsed;
  }
  if (adv_fast_) {
    return elapsed >= ADV_FAST_WINDOW_MS ? 0 : ADV_FAST_WINDOW_MS - elapsed;
  }
  return UINT32_MAX;
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

bool WatchBle::takeDisconnected() {
  const bool event = disconnected_event_;
  disconnected_event_ = false;
  return event;
}

void WatchBle::onPhoneWrite(BLEDevice, BLECharacteristic characteristic) {
  if (active_instance != nullptr) {
    active_instance->handleWrite(characteristic.value(),
                                 characteristic.valueLength());
  }
}

void WatchBle::onOtaWrite(BLEDevice, BLECharacteristic characteristic) {
  WatchBle* self = active_instance;
  if (self == nullptr || self->ota_handler_ == nullptr) {
    return;
  }
  self->connected_ = true;
  if (self->secure_ && !ATT.paired()) {
    self->refuseUnencrypted();
    return;
  }
  const int length = characteristic.valueLength();
  if (characteristic.value() != nullptr && length > 0) {
    self->ota_handler_(characteristic.value(), static_cast<size_t>(length));
  }
}

void WatchBle::handleWrite(const uint8_t* data, int length) {
  if (data == nullptr || length <= 0) {
    return;
  }
  // A write can only arrive while connected; make sure ACKs are not refused
  // because poll() has not yet observed the connection.
  connected_ = true;
  if (secure_ && !ATT.paired()) {
    refuseUnencrypted();
    return;
  }

  char message[MESSAGE_MAX + 1];
  const size_t size =
      static_cast<size_t>(length) < MESSAGE_MAX
          ? static_cast<size_t>(length)
          : MESSAGE_MAX;
  for (size_t index = 0; index < size; ++index) {
    const uint8_t value = data[index];
    message[index] =
        (value >= 0x20 && value <= 0x7E) ? static_cast<char>(value) : '?';
  }
  message[size] = '\0';

  char* fields[MAX_FIELDS]{};
  uint8_t count = 0;
  char* cursor = message;
  while (cursor != nullptr && count < MAX_FIELDS) {
    fields[count++] = cursor;
    char* separator = strchr(cursor, '|');
    if (separator != nullptr) {
      *separator = '\0';
      cursor = separator + 1;
    } else {
      cursor = nullptr;
    }
  }

  int32_t seq = 0;
  if (count < 2 || fields[0][0] == '\0' ||
      !parseInteger(fields[0], 1, 65535, seq)) {
    enqueueControl("NAK|0|PARSE");
    return;
  }

  const bool hello = strcmp(fields[1], "HELLO") == 0;
  if (!hello && last_seq_ != 0 && seq == last_seq_) {
    // Retry of a message we already accepted: the ACK was lost.
    enqueueControl("ACK|%ld", static_cast<long>(seq));
    return;
  }
  if (inbound_count_ >= INBOUND_CAPACITY) {
    enqueueControl("NAK|%ld|FULL", static_cast<long>(seq));
    return;
  }

  PhoneUpdate& update =
      inbound_[(inbound_head_ + inbound_count_) % INBOUND_CAPACITY];
  update = PhoneUpdate{};
  const ParseResult result = parse(fields + 1, count - 1, update);
  if (result != ParseResult::Ok) {
    enqueueControl("NAK|%ld|%s", static_cast<long>(seq),
                   result == ParseResult::Unknown ? "UNKNOWN" : "PARSE");
    return;
  }
  ++inbound_count_;
  last_seq_ = static_cast<uint16_t>(seq);
  enqueueControl("ACK|%ld", static_cast<long>(seq));
  if (hello) {
    enqueueControl("HELLO|%d|%s|%u", PROTOCOL_VERSION, WATCH_FIRMWARE_VERSION,
                   static_cast<unsigned>(hello_flags_));
  }
}

WatchBle::ParseResult WatchBle::parse(char* const* fields, uint8_t count,
                                      PhoneUpdate& update) {
  // fields[0] is the type; missing trailing fields read as empty.
  static const char EMPTY[] = "";
  auto field = [&](uint8_t index) -> const char* {
    return index < count ? fields[index] : EMPTY;
  };
  const char* type = fields[0];
  int32_t* n = update.number;

  if (strcmp(type, "HELLO") == 0) {
    if (!parseInteger(field(1), 0, 65535, n[0])) return ParseResult::Parse;
    update.type = PhoneUpdateType::Hello;
  } else if (strcmp(type, "TIME") == 0) {
    if (!parseUnsigned(field(1), update.utc) || update.utc < 946684800UL ||
        !parseInteger(field(2), -86400, 86400, n[0]) ||
        !parseInteger(field(3), 0, INT32_MAX, n[2]) ||
        !parseInteger(field(4), -86400, 86400, n[1])) {
      return ParseResult::Parse;
    }
    update.type = PhoneUpdateType::Time;
  } else if (strcmp(type, "NOTIFY") == 0) {
    if (!parseInteger(field(1), 1, 65535, n[0])) return ParseResult::Parse;
    copyField(update.text_a, field(2), 24);
    copyField(update.text_b, field(3), 28);
    copyField(update.text_c, field(4), 56);
    if (!parseFlag(field(5), n[1])) return ParseResult::Parse;
    update.type = PhoneUpdateType::Notify;
  } else if (strcmp(type, "NOTIFY_CLEAR") == 0) {
    if (!parseInteger(field(1), 0, 65535, n[0])) return ParseResult::Parse;
    update.type = PhoneUpdateType::NotifyClear;
  } else if (strcmp(type, "CALL") == 0) {
    if (strcmp(field(1), "RING") == 0) {
      n[0] = 1;
    } else if (strcmp(field(1), "END") == 0) {
      n[0] = 0;
    } else {
      return ParseResult::Parse;
    }
    copyField(update.text_a, field(2), 28);
    update.type = PhoneUpdateType::Call;
  } else if (strcmp(type, "WEATHER") == 0) {
    if (!parseInteger(field(1), -99, 99, n[0]) ||
        !parseInteger(field(2), -99, 99, n[1]) ||
        !parseInteger(field(3), -99, 99, n[2]) ||
        !parseInteger(field(4), -1, 255, n[3])) {
      return ParseResult::Parse;
    }
    copyField(update.text_a, field(5), 20);
    // Optional sea-level pressure in tenths of a hPa; empty = unknown.
    update.utc = 0;
    if (field(6)[0] != '\0' && !parseUnsigned(field(6), update.utc)) {
      return ParseResult::Parse;
    }
    update.type = PhoneUpdateType::Weather;
  } else if (strcmp(type, "MUSIC") == 0) {
    if (!parseInteger(field(1), 0, 2, n[0]) ||
        !parseInteger(field(2), 0, INT32_MAX, n[1]) ||
        !parseInteger(field(3), 0, INT32_MAX, n[2])) {
      return ParseResult::Parse;
    }
    copyField(update.text_a, field(4), 24);
    copyField(update.text_b, field(5), 24);
    update.type = PhoneUpdateType::Music;
  } else if (strcmp(type, "NEXT_ALARM") == 0) {
    if (!parseUnsigned(field(1), update.utc)) return ParseResult::Parse;
    update.type = PhoneUpdateType::NextAlarm;
  } else if (strcmp(type, "EVENT") == 0) {
    if (!parseUnsigned(field(1), update.utc)) return ParseResult::Parse;
    copyField(update.text_a, field(2), 28);
    update.type = PhoneUpdateType::Event;
  } else if (strcmp(type, "PHONE") == 0) {
    if (!parseInteger(field(1), 0, 100, n[0]) || !parseFlag(field(2), n[1]) ||
        !parseFlag(field(3), n[2])) {
      return ParseResult::Parse;
    }
    update.type = PhoneUpdateType::Phone;
  } else if (strcmp(type, "SETTINGS") == 0) {
    if (!parseFlag(field(1), n[0]) || !parseFlag(field(2), n[1])) {
      return ParseResult::Parse;
    }
    if (field(3)[0] == '\0') {
      n[2] = -1;  // tone not given: unchanged
    } else if (!parseInteger(field(3), 0, 5, n[2])) {
      return ParseResult::Parse;
    }
    update.type = PhoneUpdateType::Settings;
  } else if (strcmp(type, "TONE_PREVIEW") == 0) {
    if (field(1)[0] == '\0' || !parseInteger(field(1), 1, 5, n[0])) {
      return ParseResult::Parse;
    }
    update.type = PhoneUpdateType::TonePreview;
  } else if (strcmp(type, "OTA_BEGIN") == 0) {
    // The size range is judged by the application (OTA_STATE|ERROR|SIZE).
    if (field(1)[0] == '\0' || field(2)[0] == '\0' ||
        !parseInteger(field(1), 0, INT32_MAX, n[0]) ||
        !parseUnsigned(field(2), update.utc)) {
      return ParseResult::Parse;
    }
    copyField(update.text_a, field(3), 16);
    update.type = PhoneUpdateType::OtaBegin;
  } else if (strcmp(type, "OTA_END") == 0) {
    update.type = PhoneUpdateType::OtaEnd;
  } else if (strcmp(type, "OTA_ABORT") == 0) {
    update.type = PhoneUpdateType::OtaAbort;
  } else if (strcmp(type, "FIND_WATCH") == 0) {
    update.type = PhoneUpdateType::FindWatch;
  } else if (strcmp(type, "CAMERA") == 0) {
    // CAMERA|state|number|detail; values match CameraState in watch_ui.h.
    static constexpr const char* STATES[] = {"CLOSED", "OPENING", "READY",
                                             "COUNTDOWN", "SAVED", "ERROR"};
    n[0] = -1;
    for (uint8_t i = 0; i < sizeof(STATES) / sizeof(STATES[0]); ++i) {
      if (strcmp(field(1), STATES[i]) == 0) {
        n[0] = i;
      }
    }
    if (n[0] < 0) return ParseResult::Parse;
    if (field(2)[0] != '\0' && !parseInteger(field(2), 0, 9999, n[1])) {
      return ParseResult::Parse;
    }
    copyField(update.text_a, field(3), 20);
    update.type = PhoneUpdateType::Camera;
  } else if (strcmp(type, "REQUEST") == 0) {
    if (strcmp(field(1), "STATUS") == 0) {
      update.type = PhoneUpdateType::RequestStatus;
    } else if (strcmp(field(1), "HISTORY") == 0) {
      update.type = PhoneUpdateType::RequestHistory;
    } else {
      return ParseResult::Parse;
    }
  } else if (strcmp(type, "STEPS_RESET") == 0) {
    update.type = PhoneUpdateType::StepsReset;
  } else if (strcmp(type, "SLEEP_START") == 0) {
    update.type = PhoneUpdateType::SleepStart;
  } else if (strcmp(type, "SLEEP_STOP") == 0) {
    update.type = PhoneUpdateType::SleepStop;
  } else if (strcmp(type, "SLEEP_PAUSE") == 0) {
    if (!parseInteger(field(1), INT32_MIN + 1, INT32_MAX, n[0])) {
      return ParseResult::Parse;
    }
    update.type = PhoneUpdateType::SleepPause;
  } else if (strcmp(type, "SLEEP_ALARM") == 0) {
    if (!parseInteger(field(1), INT32_MIN + 1, INT32_MAX, n[0])) {
      return ParseResult::Parse;
    }
    update.type = PhoneUpdateType::SleepAlarm;
  } else if (strcmp(type, "SLEEP_ALARM_STOP") == 0) {
    update.type = PhoneUpdateType::SleepAlarmStop;
  } else if (strcmp(type, "SLEEP_NOTIFY") == 0) {
    copyField(update.text_a, field(1), 28);
    copyField(update.text_b, field(2), 28);
    update.type = PhoneUpdateType::SleepNotify;
  } else if (strcmp(type, "SLEEP_HINT") == 0) {
    if (!parseInteger(field(1), 0, 65535, n[0])) return ParseResult::Parse;
    update.type = PhoneUpdateType::SleepHint;
  } else {
    return ParseResult::Unknown;
  }
  return ParseResult::Ok;
}

void WatchBle::enqueueControl(const char* format, ...) {
  // ACK/NAK must not be lost to a full queue: drop the oldest queued
  // notification instead, since a missing ACK stalls the phone for 1.5 s.
  if (outbound_count_ >= OUTBOUND_CAPACITY) {
    outbound_head_ =
        static_cast<uint8_t>((outbound_head_ + 1) % OUTBOUND_CAPACITY);
    --outbound_count_;
  }
  OutboundMessage& slot =
      outbound_[(outbound_head_ + outbound_count_) % OUTBOUND_CAPACITY];
  char buffer[MESSAGE_MAX + 1];
  va_list args;
  va_start(args, format);
  const int written = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (written < 0) {
    return;
  }
  const size_t length =
      static_cast<size_t>(written) < MESSAGE_MAX
          ? static_cast<size_t>(written)
          : MESSAGE_MAX;
  memcpy(slot.data, buffer, length);
  slot.length = static_cast<uint8_t>(length);
  ++outbound_count_;
}

void WatchBle::clearOutbound() {
  outbound_head_ = 0;
  outbound_count_ = 0;
  sending_active_ = false;
  sending_offset_ = 0;
}

// Sends queued messages in order, fragmenting as PROTOCOL.md requires: a
// message of at most 20 bytes goes out as is; a longer one as chunks of at
// most 19 payload bytes, each prefixed '+', the last one '='. The message
// being sent is held in sending_ until its last chunk has gone, so chunks of
// two messages can never interleave.
void WatchBle::drainOutbound() {
  if (!connected_ || !watch_to_phone.subscribed()) {
    return;
  }
  while (true) {
    if (!sending_active_) {
      if (outbound_count_ == 0) {
        return;
      }
      // Pop before sending: writeValue() can poll the controller while it
      // waits for HCI buffer space, which re-enters handleWrite() and queues
      // ACKs (possibly displacing the head) underneath this loop.
      sending_ = outbound_[outbound_head_];
      outbound_head_ =
          static_cast<uint8_t>((outbound_head_ + 1) % OUTBOUND_CAPACITY);
      --outbound_count_;
      sending_offset_ = 0;
      sending_active_ = true;
      if (sending_.length == 0) {
        sending_active_ = false;
        continue;
      }
    }

    uint8_t chunk[NOTIFICATION_MAX];
    size_t chunk_length = 0;
    size_t consumed = 0;
    if (sending_offset_ == 0 && sending_.length <= NOTIFICATION_MAX) {
      consumed = sending_.length;
      memcpy(chunk, sending_.data, consumed);
      chunk_length = consumed;
    } else {
      const size_t remaining = sending_.length - sending_offset_;
      consumed = remaining < NOTIFICATION_MAX - 1 ? remaining
                                                  : NOTIFICATION_MAX - 1;
      chunk[0] = consumed == remaining ? '=' : '+';
      memcpy(chunk + 1, sending_.data + sending_offset_, consumed);
      chunk_length = consumed + 1;
    }
    // Account for the chunk before sending it, for the same re-entrancy
    // reason as the pop above.
    sending_offset_ = static_cast<uint8_t>(sending_offset_ + consumed);
    if (sending_offset_ >= sending_.length) {
      sending_active_ = false;
    }
    if (watch_to_phone.writeValue(chunk, static_cast<int>(chunk_length)) ==
        0) {
      // Link dropped mid-send; poll() clears the queue on disconnect.
      sending_active_ = false;
      return;
    }
    // HCI flow control may spin briefly while controller buffers drain.
    WatchWatchdog::feed();
  }
}
