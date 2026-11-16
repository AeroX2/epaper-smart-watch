#include "watch_rtc.h"

#include <stdio.h>
#include <string.h>

namespace {

uint8_t parseTwo(const char* value) {
  return static_cast<uint8_t>((value[0] - '0') * 10 + value[1] - '0');
}

uint8_t compileMonth() {
  static constexpr const char* MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
  for (uint8_t month = 0; month < 12; ++month) {
    if (strncmp(__DATE__, MONTHS + month * 3, 3) == 0) {
      return month + 1;
    }
  }
  return 1;
}

int64_t daysFromCivil(int32_t year, uint8_t month, uint8_t day) {
  year -= month <= 2;
  const int32_t era = (year >= 0 ? year : year - 399) / 400;
  const uint32_t year_of_era = static_cast<uint32_t>(year - era * 400);
  const uint32_t day_of_year =
      (153U * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const uint32_t day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return static_cast<int64_t>(era) * 146097 + day_of_era - 719468;
}

uint8_t weekdayForDate(uint16_t year, uint8_t month, uint8_t day) {
  const int64_t days = daysFromCivil(year, month, day);
  return static_cast<uint8_t>((days + 4) % 7);
}


// Backup registers not used by the Arduino core (DR1/DR4/DR10), STM32RTC
// (DR6/DR7), or the DFU request (DR19).
constexpr uint32_t OFFSET_MAGIC = 0x5A0FF5E7UL;
#define WATCH_OFFSET_MAGIC_REGISTER (RTC->BKP12R)
#define WATCH_OFFSET_VALUE_REGISTER (RTC->BKP13R)

}  // namespace

bool WatchRtc::begin(Print& out) {
  HAL_PWR_EnableBkUpAccess();
  LL_RCC_LSE_Enable();
  const uint32_t lse_started = millis();
  while (!LL_RCC_LSE_IsReady() && millis() - lse_started < 1500) {
    delay(1);
  }
  lse_ready_ = LL_RCC_LSE_IsReady();
  rtc_.setClockSource(lse_ready_ ? STM32RTC::LSE_CLOCK
                                 : STM32RTC::LSI_CLOCK);
  rtc_.begin(false, STM32RTC::HOUR_24);
  if (!rtc_.isTimeSet()) {
    rtc_.setEpoch(compileEpoch());
    out.println(F("RTC was unset; initialized from firmware build time."));
  }
  // The magic is written whenever the time is set (phone or console) and is
  // lost with the backup domain, so it doubles as "the time is real".
  if (WATCH_OFFSET_MAGIC_REGISTER == OFFSET_MAGIC) {
    utc_offset_ = static_cast<int32_t>(WATCH_OFFSET_VALUE_REGISTER);
    time_trusted_ = true;
  }
  ready_ = true;
  out.print(lse_ready_ ? F("[PASS] ") : F("[WARN] "));
  out.print(F("STM32 RTC using "));
  out.println(lse_ready_ ? F("LSE") : F("LSI fallback; check 32.768 kHz crystal"));
  print(out);
  return true;
}

void WatchRtc::updateModel(WatchUiModel& model) {
  if (!ready_) {
    model.time_valid = false;
    return;
  }
  model.hour = rtc_.getHours();
  model.minute = rtc_.getMinutes();
  model.second = rtc_.getSeconds();
  model.day = rtc_.getDay();
  model.month = rtc_.getMonth();
  model.year = static_cast<uint16_t>(2000 + rtc_.getYear());
  model.weekday = weekdayForDate(model.year, model.month, model.day);
  model.local_epoch = static_cast<uint32_t>(rtc_.getEpoch());
  model.utc_offset_seconds = utc_offset_;
  model.time_valid = true;
}

bool WatchRtc::setDateTime(uint16_t year, uint8_t month, uint8_t day,
                           uint8_t hour, uint8_t minute, uint8_t second) {
  if (!ready_ || year < 2000 || year > 2099 || month < 1 || month > 12 ||
      day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
    return false;
  }
  rtc_.setDate(day, month, static_cast<uint8_t>(year - 2000));
  rtc_.setTime(hour, minute, second);
  time_trusted_ = true;
  storeOffset();
  return true;
}

bool WatchRtc::setEpoch(time_t epoch_value) {
  if (!ready_ || epoch_value < 946684800) {
    return false;
  }
  rtc_.setEpoch(epoch_value);
  return true;
}

bool WatchRtc::setUtc(uint32_t utc, int32_t offset_seconds) {
  const int64_t local = static_cast<int64_t>(utc) + offset_seconds;
  if (!ready_ || local < 946684800LL || local > 4102444799LL) {
    return false;
  }
  rtc_.setEpoch(static_cast<time_t>(local));
  utc_offset_ = offset_seconds;
  time_trusted_ = true;
  storeOffset();
  return true;
}

time_t WatchRtc::epoch() {
  return ready_ ? rtc_.getEpoch() : 0;
}

void WatchRtc::setTransition(uint32_t transition_utc, int32_t offset_after) {
  transition_utc_ = transition_utc;
  transition_offset_ = offset_after;
}

bool WatchRtc::serviceTransition() {
  if (transition_utc_ == 0 || !ready_) {
    return false;
  }
  const int64_t local = static_cast<int64_t>(rtc_.getEpoch());
  const int64_t utc_now = local - utc_offset_;
  if (utc_now < static_cast<int64_t>(transition_utc_)) {
    return false;
  }
  transition_utc_ = 0;
  if (transition_offset_ == utc_offset_) {
    return false;
  }
  rtc_.setEpoch(static_cast<time_t>(utc_now + transition_offset_));
  utc_offset_ = transition_offset_;
  storeOffset();
  return true;
}

int32_t WatchRtc::utcOffset() const {
  return utc_offset_;
}

void WatchRtc::storeOffset() {
  HAL_PWR_EnableBkUpAccess();
  WATCH_OFFSET_VALUE_REGISTER = static_cast<uint32_t>(utc_offset_);
  WATCH_OFFSET_MAGIC_REGISTER = OFFSET_MAGIC;
}

uint64_t WatchRtc::milliseconds() {
  if (!ready_) {
    return 0;
  }
  uint32_t subseconds = 0;
  const time_t seconds = rtc_.getEpoch(&subseconds);
  return static_cast<uint64_t>(seconds) * 1000ULL + subseconds;
}

void WatchRtc::restoreAfterBackupReset(time_t saved_epoch, uint32_t elapsed_ms,
                                       Print& out) {
  rtc_.setClockSource(lse_ready_ ? STM32RTC::LSE_CLOCK
                                 : STM32RTC::LSI_CLOCK);
  rtc_.begin(true, STM32RTC::HOUR_24);
  rtc_.setEpoch(saved_epoch + elapsed_ms / 1000);
  // The backup-domain reset also cleared the stored UTC offset. Only put it
  // back if the time was real: the magic also marks the time as trusted.
  if (time_trusted_) {
    storeOffset();
  }
  ready_ = true;
  out.println(F("RTC restored after STM32WB radio backup-domain reset."));
}

void WatchRtc::print(Print& out) {
  if (!ready_) {
    out.println(F("[FAIL] RTC is not initialized"));
    return;
  }
  char value[32];
  snprintf(value, sizeof(value), "%04u-%02u-%02u %02u:%02u:%02u",
           2000U + rtc_.getYear(), rtc_.getMonth(), rtc_.getDay(),
           rtc_.getHours(), rtc_.getMinutes(), rtc_.getSeconds());
  out.print(F("RTC (local): "));
  out.print(value);
  out.print(F("  UTC offset s: "));
  out.println(utc_offset_);
}

bool WatchRtc::usingLse() const {
  return lse_ready_;
}

bool WatchRtc::timeTrusted() const {
  return time_trusted_;
}

time_t WatchRtc::compileEpoch() {
  const uint16_t year =
      static_cast<uint16_t>((__DATE__[7] - '0') * 1000 +
                            (__DATE__[8] - '0') * 100 +
                            (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0'));
  const uint8_t day = static_cast<uint8_t>(
      (__DATE__[4] == ' ' ? 0 : __DATE__[4] - '0') * 10 +
      (__DATE__[5] - '0'));
  const uint8_t hour = parseTwo(__TIME__);
  const uint8_t minute = parseTwo(__TIME__ + 3);
  const uint8_t second = parseTwo(__TIME__ + 6);
  const int64_t seconds =
      daysFromCivil(year, compileMonth(), day) * 86400 +
      static_cast<int64_t>(hour) * 3600 + minute * 60 + second;
  return static_cast<time_t>(seconds);
}
