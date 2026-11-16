#include "watch_flash.h"

#include <stdio.h>

#include "board_pins.h"
#include "watch_watchdog.h"

namespace {

constexpr uint8_t CMD_PAGE_PROGRAM = 0x02;
constexpr uint8_t CMD_READ = 0x03;
constexpr uint8_t CMD_READ_STATUS = 0x05;
constexpr uint8_t CMD_WRITE_ENABLE = 0x06;
constexpr uint8_t CMD_SECTOR_ERASE = 0x20;
constexpr uint8_t CMD_JEDEC_ID = 0x9F;
constexpr uint8_t CMD_RELEASE_POWER_DOWN = 0xAB;
constexpr uint8_t CMD_DEEP_POWER_DOWN = 0xB9;

constexpr uint8_t STATUS_WIP = 0x01;
constexpr uint8_t STATUS_WEL = 0x02;

// Port/mask pairs resolved once so the bit loop is a few register writes.
struct FastPin {
  GPIO_TypeDef* port = nullptr;
  uint32_t mask = 0;

  void resolve(uint32_t pin) {
    const PinName name = digitalPinToPinName(pin);
    port = get_GPIO_Port(STM_PORT(name));
    mask = STM_GPIO_PIN(name);
  }
  inline void high() const { port->BSRR = mask; }
  inline void low() const { port->BRR = mask; }
  inline bool read() const { return (port->IDR & mask) != 0; }
};

FastPin clk;
FastPin mosi;
FastPin miso;

// Roughly 60 ns at 64 MHz. Keeps the bit-banged clock in the low-MHz range,
// far inside the flash limits and tolerant of slow edges on the board.
inline void halfBitDelay() {
  __NOP();
  __NOP();
  __NOP();
  __NOP();
}

}  // namespace

void WatchFlash::begin() {
  // WP# and HOLD# must be high before any command: with QE = 0 they are
  // active-low inputs that would otherwise block writes or pause the bus.
  digitalWrite(BoardPins::FLASH_WP, HIGH);
  pinMode(BoardPins::FLASH_WP, OUTPUT);
  digitalWrite(BoardPins::FLASH_HOLD, HIGH);
  pinMode(BoardPins::FLASH_HOLD, OUTPUT);
  digitalWrite(BoardPins::FLASH_CS, HIGH);
  pinMode(BoardPins::FLASH_CS, OUTPUT);
  digitalWrite(BoardPins::FLASH_CLK, LOW);
  pinMode(BoardPins::FLASH_CLK, OUTPUT);
  digitalWrite(BoardPins::FLASH_MOSI, LOW);
  pinMode(BoardPins::FLASH_MOSI, OUTPUT);
  // The pull-up makes a missing chip read as 0xFF (absent / busy forever,
  // caught by the timeouts) and stops IO1 floating while the chip sleeps.
  pinMode(BoardPins::FLASH_MISO, INPUT_PULLUP);

  clk.resolve(BoardPins::FLASH_CLK);
  mosi.resolve(BoardPins::FLASH_MOSI);
  miso.resolve(BoardPins::FLASH_MISO);
  configured_ = clk.port != nullptr && mosi.port != nullptr &&
                miso.port != nullptr;
  hold_count_ = 0;
  if (!configured_) {
    present_ = false;
    return;
  }

  // A reset can interrupt an erase or program; the chip ignores everything
  // but the status read until it finishes, so let it before reading the ID.
  wake();
  waitReady(BOOT_BUSY_TIMEOUT_MS);
  sleep();
  probe();
}

bool WatchFlash::probe() {
  if (!configured_) {
    present_ = false;
    return false;
  }
  acquire();
  jedec_id_ = readIdRaw();
  release();
  present_ = jedec_id_ != 0x000000UL && jedec_id_ != 0xFFFFFFUL;
  return present_;
}

uint32_t WatchFlash::capacityBytes() const {
  const uint8_t code = static_cast<uint8_t>(jedec_id_ & 0xFF);
  if (!present_ || code < 0x10 || code > 0x18) {
    return 0;
  }
  return 1UL << code;
}

void WatchFlash::acquire() {
  if (!configured_) {
    return;
  }
  if (hold_count_ == 0) {
    wake();
  }
  if (hold_count_ < UINT8_MAX) {
    ++hold_count_;
  }
}

void WatchFlash::release() {
  if (!configured_ || hold_count_ == 0) {
    return;
  }
  if (--hold_count_ == 0) {
    sleep();
  }
}

void WatchFlash::select() {
  clk.low();
  digitalWrite(BoardPins::FLASH_CS, LOW);
  halfBitDelay();
}

void WatchFlash::deselect() {
  clk.low();
  digitalWrite(BoardPins::FLASH_CS, HIGH);
  // CS# high time between commands (tSHSL, tens of ns) with ample margin.
  delayMicroseconds(1);
}

void WatchFlash::sendByte(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; ++bit) {
    if ((value & 0x80U) != 0) {
      mosi.high();
    } else {
      mosi.low();
    }
    value = static_cast<uint8_t>(value << 1);
    halfBitDelay();
    clk.high();
    halfBitDelay();
    clk.low();
  }
}

uint8_t WatchFlash::receiveByte() {
  uint8_t value = 0;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    // The flash shifts data out on the falling edge; it is stable well
    // before this rising edge, where it is sampled.
    halfBitDelay();
    clk.high();
    value = static_cast<uint8_t>((value << 1) | (miso.read() ? 1U : 0U));
    halfBitDelay();
    clk.low();
  }
  return value;
}

void WatchFlash::sendAddress(uint8_t command, uint32_t address) {
  sendByte(command);
  sendByte(static_cast<uint8_t>(address >> 16));
  sendByte(static_cast<uint8_t>(address >> 8));
  sendByte(static_cast<uint8_t>(address));
}

void WatchFlash::wake() {
  select();
  sendByte(CMD_RELEASE_POWER_DOWN);
  deselect();
  // tRES1 is 30 us; the chip ignores commands until it has elapsed.
  delayMicroseconds(100);
}

void WatchFlash::sleep() {
  select();
  sendByte(CMD_DEEP_POWER_DOWN);
  deselect();
  delayMicroseconds(10);
}

uint8_t WatchFlash::readStatusRaw() {
  select();
  sendByte(CMD_READ_STATUS);
  const uint8_t status = receiveByte();
  deselect();
  return status;
}

uint32_t WatchFlash::readIdRaw() {
  select();
  sendByte(CMD_JEDEC_ID);
  uint32_t id = receiveByte();
  id = (id << 8) | receiveByte();
  id = (id << 8) | receiveByte();
  deselect();
  return id;
}

uint8_t WatchFlash::statusRegister() {
  if (!configured_ || !present_) {
    return 0xFF;
  }
  acquire();
  const uint8_t status = readStatusRaw();
  release();
  return status;
}

bool WatchFlash::waitReady(uint32_t timeout_ms) {
  const uint32_t started = millis();
  while (true) {
    const uint8_t status = readStatusRaw();
    // 0xFF is what a missing or unpowered chip reads as: keep "busy".
    if (status != 0xFF && (status & STATUS_WIP) == 0) {
      return true;
    }
    if (millis() - started >= timeout_ms) {
      return false;
    }
    WatchWatchdog::feed();
    delayMicroseconds(200);
  }
}

bool WatchFlash::writeEnable() {
  select();
  sendByte(CMD_WRITE_ENABLE);
  deselect();
  // A chip that did not latch WEL would silently ignore the write.
  const uint8_t status = readStatusRaw();
  return status != 0xFF && (status & STATUS_WEL) != 0;
}

bool WatchFlash::rangeValid(uint32_t address, size_t length) const {
  return configured_ && present_ && address < CAPACITY &&
         length <= CAPACITY - address;
}

bool WatchFlash::read(uint32_t address, uint8_t* data, size_t length) {
  if (data == nullptr || !rangeValid(address, length)) {
    return false;
  }
  if (length == 0) {
    return true;
  }
  acquire();
  const bool ok = waitReady(PROGRAM_TIMEOUT_MS);
  if (ok) {
    select();
    sendAddress(CMD_READ, address);
    for (size_t index = 0; index < length; ++index) {
      data[index] = receiveByte();
    }
    deselect();
  }
  release();
  return ok;
}

bool WatchFlash::program(uint32_t address, const uint8_t* data,
                         size_t length) {
  if (data == nullptr || !rangeValid(address, length)) {
    return false;
  }
  acquire();
  bool ok = waitReady(PROGRAM_TIMEOUT_MS);
  while (ok && length > 0) {
    const size_t page_room = PAGE_SIZE - (address & (PAGE_SIZE - 1));
    const size_t chunk = length < page_room ? length : page_room;
    ok = writeEnable();
    if (!ok) {
      break;
    }
    select();
    sendAddress(CMD_PAGE_PROGRAM, address);
    for (size_t index = 0; index < chunk; ++index) {
      sendByte(data[index]);
    }
    deselect();
    ok = waitReady(PROGRAM_TIMEOUT_MS);
    address += chunk;
    data += chunk;
    length -= chunk;
  }
  release();
  return ok;
}

bool WatchFlash::eraseSector(uint32_t address) {
  if (!rangeValid(address, 1)) {
    return false;
  }
  address &= ~(SECTOR_SIZE - 1);
  acquire();
  bool ok = waitReady(PROGRAM_TIMEOUT_MS) && writeEnable();
  if (ok) {
    select();
    sendAddress(CMD_SECTOR_ERASE, address);
    deselect();
    ok = waitReady(ERASE_TIMEOUT_MS);
  }
  release();
  return ok;
}

bool WatchFlash::prepareForReset(uint32_t timeout_ms) {
  if (!configured_) {
    return false;
  }
  // Deliberately unbalanced: the chip stays awake through the reset.
  acquire();
  return waitReady(timeout_ms);
}

void WatchFlash::printStatus(Print& out) {
  probe();
  char id[8];
  snprintf(id, sizeof(id), "%06lX", static_cast<unsigned long>(jedec_id_));
  if (!present_) {
    out.print(F("[WARN] External flash not detected (JEDEC ID 0x"));
    out.print(id);
    out.println(F("); settings, bond and step history are RAM-only."));
    return;
  }
  out.print(jedec_id_ == EXPECTED_JEDEC_ID ? F("[PASS] ") : F("[WARN] "));
  out.print(F("External flash JEDEC ID 0x"));
  out.print(id);
  if (jedec_id_ != EXPECTED_JEDEC_ID) {
    out.print(F(" (expected 0x0B4016 for XT25F32F)"));
  }
  out.print(F(", "));
  out.print(capacityBytes() / 1024UL);
  out.print(F(" KiB, status 0x"));
  out.println(statusRegister(), HEX);
}
