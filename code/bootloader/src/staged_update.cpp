/*
 * Staged firmware update installer (bootloader half of
 * code/EXTERNAL_FLASH.md).
 *
 * Runs from bootDecision(), i.e. from .preinit_array: the Arduino runtime has
 * not configured clocks, SysTick or the HAL yet, and no interrupt source is
 * enabled. Everything here is therefore register-level and tick-free:
 *
 *   - the XT25F32F is driven with bit-banged single-line SPI (mode 0) on
 *     PA3 CLK / PD3 NCS / PD4 MOSI / PD5 MISO, with PD6 WP# and PD7 HOLD#
 *     held high as plain outputs;
 *   - internal flash is erased/programmed with the same register sequence the
 *     HAL uses (page erase, doubleword program), without the HAL's
 *     HAL_GetTick() timeouts;
 *   - every wait is a loop with an iteration bound, never a time-based
 *     timeout.
 *
 * Nothing here touches the pins configureSafeGpio() parked (PA6, PA15, PC3,
 * PE0, PC10): GPIOA is only written through BSRR/BRR for PA3 and through
 * read-modify-write of the PA3 fields.
 */
#include "staged_update.h"

#include "ota_format.h"
#include "stm32wbxx_hal.h"

extern "C" volatile uint32_t g_flash_ecc_faults; /* stm32wbxx_it.c */

namespace {

static_assert(ota::kSramStart == SRAM1_BASE, "SRAM base mismatch");
static_assert(FLASH_PAGE_SIZE == 4096U, "unexpected flash page size");

constexpr uint32_t kPageSize = 4096UL;
constexpr uint32_t kFirstAppPage = (ota::kAppStart - FLASH_BASE) / kPageSize;
constexpr uint32_t kAppPageCount = (ota::kAppEnd - ota::kAppStart) / kPageSize;
constexpr uint32_t kChunk = 256UL; /* stack buffer; multiple of 8 */

/* ---- loop bounds -------------------------------------------------------
 * One spin() iteration is a volatile load/add/store/compare/branch: roughly
 * 8-12 CPU cycles, so about 2-3 us at the 4 MHz reset clock (the same
 * assumption recoveryButtonPressed() already makes).
 *
 * The probe runs at the reset clock, before the clock is raised:
 *   kProbeAttempts x (kReleaseSpin + kProbeRetrySpin + ~7 SPI bytes)
 *   = 10 x (~0.5 ms + ~1 ms + ~0.3 ms) ~= 20 ms worst case for an absent,
 *   dead or stuck-busy chip, after which the normal boot continues. */
constexpr uint32_t kReleaseSpin = 200U;    /* >= 150 us; chip needs 30 us */
constexpr uint32_t kProbeRetrySpin = 400U; /* ~1 ms between probe attempts */
constexpr uint32_t kProbeAttempts = 10U;
/* External page program (4 bytes): typ < 1 ms, max a few ms. One poll is a
 * 2-byte SPI transfer, >= ~5 us at 32 MHz and ~60 us at 4 MHz, so this bound
 * is >= 100 ms and at most about 1.5 s. */
constexpr uint32_t kExtWritePolls = 20000U;
/* Internal flash: one poll of FLASH->SR is >= 3 cycles. Page erase takes
 * ~22 ms and a doubleword ~85 us; the bounds are >= 370 ms / >= 18 ms at
 * 32 MHz (about 8x longer if the clock could not be raised). */
constexpr uint32_t kIntEraseBound = 4000000U;
constexpr uint32_t kIntProgramBound = 200000U;
constexpr uint32_t kClockBound = 100000U;

/* ---- pins --------------------------------------------------------------- */
constexpr uint32_t kClk = GPIO_PIN_3;  /* PA3 */
constexpr uint32_t kNcs = GPIO_PIN_3;  /* PD3 */
constexpr uint32_t kMosi = GPIO_PIN_4; /* PD4 / IO0 */
constexpr uint32_t kMiso = GPIO_PIN_5; /* PD5 / IO1 */
constexpr uint32_t kWp = GPIO_PIN_6;   /* PD6 / IO2 */
constexpr uint32_t kHold = GPIO_PIN_7; /* PD7 / IO3 */
constexpr uint32_t kMisoPos = 5U;

constexpr uint32_t kPortDFieldMask =
    GPIO_MODER_MODE3_Msk | GPIO_MODER_MODE4_Msk | GPIO_MODER_MODE5_Msk |
    GPIO_MODER_MODE6_Msk | GPIO_MODER_MODE7_Msk;

void spin(uint32_t iterations) {
  for (volatile uint32_t i = 0; i < iterations; i = i + 1U) {
  }
}

/* ---- external flash: pins ------------------------------------------------ */

struct PinState {
  bool gpiod_clock_was_on;
};

void extPinsAcquire(PinState& saved) {
  saved.gpiod_clock_was_on = (RCC->AHB2ENR & RCC_AHB2ENR_GPIODEN) != 0U;
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIODEN;
  (void)RCC->AHB2ENR;

  /* Set the idle levels before the pins become outputs: NCS, WP#, HOLD# high,
   * MOSI and CLK low. */
  GPIOD->BSRR = kNcs | kWp | kHold;
  GPIOD->BRR = kMosi;
  GPIOA->BRR = kClk;

  /* Pull-up on MISO so an absent chip reads a steady 0xFF. */
  GPIOD->PUPDR = (GPIOD->PUPDR & ~kPortDFieldMask) |
                 (1UL << GPIO_PUPDR_PUPD5_Pos);
  /* Medium speed on the driven pins (the default "low" is marginal for a few
   * MHz at low supply voltage). */
  GPIOD->OSPEEDR = (GPIOD->OSPEEDR & ~kPortDFieldMask) |
                   (1UL << GPIO_OSPEEDR_OSPEED3_Pos) |
                   (1UL << GPIO_OSPEEDR_OSPEED4_Pos);
  GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~GPIO_OSPEEDR_OSPEED3_Msk) |
                   (1UL << GPIO_OSPEEDR_OSPEED3_Pos);

  /* PD3, PD4, PD6, PD7 outputs; PD5 input. PA3 output. */
  GPIOD->MODER = (GPIOD->MODER & ~kPortDFieldMask) |
                 (1UL << GPIO_MODER_MODE3_Pos) |
                 (1UL << GPIO_MODER_MODE4_Pos) |
                 (1UL << GPIO_MODER_MODE6_Pos) |
                 (1UL << GPIO_MODER_MODE7_Pos);
  GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODE3_Msk) |
                 (1UL << GPIO_MODER_MODE3_Pos);
}

/* Back to the reset state (analog, no pull, low speed, output latch 0) so the
 * application's own driver starts from a clean slate. */
void extPinsRelease(const PinState& saved) {
  GPIOD->MODER |= kPortDFieldMask;
  GPIOA->MODER |= GPIO_MODER_MODE3_Msk;
  GPIOD->PUPDR &= ~kPortDFieldMask;
  GPIOD->OSPEEDR &= ~kPortDFieldMask;
  GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED3_Msk;
  GPIOD->BRR = kNcs | kMosi | kWp | kHold;
  GPIOA->BRR = kClk;
  if (!saved.gpiod_clock_was_on) {
    RCC->AHB2ENR &= ~RCC_AHB2ENR_GPIODEN;
  }
}

/* ---- external flash: bit-banged SPI mode 0 -------------------------------- */

inline void extSelect() {
  GPIOD->BRR = kNcs;
  __NOP();
  __NOP();
}

inline void extDeselect() {
  GPIOD->BSRR = kNcs;
  /* NCS high time between commands. */
  __NOP();
  __NOP();
  __NOP();
  __NOP();
}

void extSend(uint8_t value) {
  for (uint32_t bit = 0x80U; bit != 0U; bit >>= 1) {
    if ((value & bit) != 0U) {
      GPIOD->BSRR = kMosi;
    } else {
      GPIOD->BRR = kMosi;
    }
    GPIOA->BSRR = kClk;
    __NOP();
    GPIOA->BRR = kClk;
  }
  GPIOD->BRR = kMosi;
}

uint8_t extReceive() {
  uint32_t value = 0U;
  for (uint32_t bit = 0U; bit < 8U; ++bit) {
    /* The chip changes MISO after the falling edge; sample after the rising
     * edge, then give it the next falling edge. */
    GPIOA->BSRR = kClk;
    value = (value << 1) | ((GPIOD->IDR >> kMisoPos) & 1U);
    GPIOA->BRR = kClk;
  }
  return static_cast<uint8_t>(value);
}

void extSendAddress(uint32_t address) {
  extSend(static_cast<uint8_t>(address >> 16));
  extSend(static_cast<uint8_t>(address >> 8));
  extSend(static_cast<uint8_t>(address));
}

/* ---- external flash: commands --------------------------------------------- */

void extReleasePowerDown() {
  extSelect();
  extSend(0xABU);
  extDeselect();
  spin(kReleaseSpin);
}

uint8_t extReadStatus() {
  extSelect();
  extSend(0x05U);
  const uint8_t status = extReceive();
  extDeselect();
  return status;
}

uint32_t extReadJedecId() {
  extSelect();
  extSend(0x9FU);
  uint32_t id = extReceive();
  id = (id << 8) | extReceive();
  id = (id << 8) | extReceive();
  extDeselect();
  return id;
}

void extRead(uint32_t address, uint8_t* buffer, uint32_t length) {
  extSelect();
  extSend(0x03U);
  extSendAddress(address);
  for (uint32_t i = 0U; i < length; ++i) {
    buffer[i] = extReceive();
  }
  extDeselect();
}

/* Bounded busy wait. A status of 0xFF (no chip driving MISO) counts as busy,
 * so a dead chip runs out the bound instead of looking idle. */
bool extWaitReady(uint32_t max_polls) {
  for (uint32_t poll = 0U; poll < max_polls; ++poll) {
    const uint8_t status = extReadStatus();
    if (status != 0xFFU && (status & 0x01U) == 0U) {
      return true;
    }
  }
  return false;
}

/* Release from deep power-down and confirm a live, idle chip answers. Bounded
 * to kProbeAttempts tries (about 20 ms at the reset clock). */
bool extProbe() {
  for (uint32_t attempt = 0U; attempt < kProbeAttempts; ++attempt) {
    extReleasePowerDown();
    const uint8_t status = extReadStatus();
    if (status != 0xFFU && (status & 0x01U) == 0U) {
      const uint32_t id = extReadJedecId();
      if (id != 0x000000UL && id != 0xFFFFFFUL) {
        return true;
      }
    }
    spin(kProbeRetrySpin);
  }
  return false;
}

/* The only write this bootloader ever makes to the external flash: the 4-byte
 * `result` word in the OTA header. Returns true when it reads back. */
bool extWriteResult(uint32_t code) {
  const uint32_t address = ota::kHeaderAddress + ota::kResultOffset;
  if (!extWaitReady(kExtWritePolls)) {
    return false;
  }
  extSelect();
  extSend(0x06U); /* write enable */
  extDeselect();

  extSelect();
  extSend(0x02U); /* page program */
  extSendAddress(address);
  extSend(static_cast<uint8_t>(code));
  extSend(static_cast<uint8_t>(code >> 8));
  extSend(static_cast<uint8_t>(code >> 16));
  extSend(static_cast<uint8_t>(code >> 24));
  extDeselect();

  if (!extWaitReady(kExtWritePolls)) {
    return false;
  }
  uint8_t readback[4];
  extRead(address, readback, sizeof(readback));
  return ota::readLe32(readback) == code;
}

/* ---- system clock --------------------------------------------------------- */

struct ClockState {
  uint32_t msi_range;
  uint32_t latency;
  bool raised;
};

/* The install moves ~1.5 MB through the bit-banged bus, so run it from MSI at
 * 32 MHz (one flash wait state) instead of the 4 MHz reset clock. Only done
 * when the clock tree is in its reset shape; otherwise the install simply
 * runs slower. */
void clockRaise(ClockState& saved) {
  saved.msi_range = RCC->CR & RCC_CR_MSIRANGE;
  saved.latency = FLASH->ACR & FLASH_ACR_LATENCY;
  saved.raised = false;

  const bool on_msi = (RCC->CFGR & RCC_CFGR_SWS) == 0U;
  const bool msi_ready = (RCC->CR & RCC_CR_MSIRDY) != 0U;
  const bool range1 = (PWR->CR1 & PWR_CR1_VOS) == PWR_CR1_VOS_0;
  const bool undivided =
      (RCC->CFGR & (RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) == 0U;
  if (!on_msi || !msi_ready || !range1 || !undivided ||
      saved.msi_range >= RCC_CR_MSIRANGE_10 || saved.latency != 0U) {
    return;
  }

  FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_1WS;
  uint32_t bound = kClockBound;
  while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_1WS) {
    if (--bound == 0U) {
      FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | saved.latency;
      return;
    }
  }

  RCC->CR = (RCC->CR & ~RCC_CR_MSIRANGE) | RCC_CR_MSIRANGE_10;
  bound = kClockBound;
  while ((RCC->CR & RCC_CR_MSIRDY) == 0U && --bound != 0U) {
  }
  saved.raised = true;
}

void clockRestore(const ClockState& saved) {
  if (!saved.raised) {
    return;
  }
  uint32_t bound = kClockBound;
  while ((RCC->CR & RCC_CR_MSIRDY) == 0U && --bound != 0U) {
  }
  RCC->CR = (RCC->CR & ~RCC_CR_MSIRANGE) | saved.msi_range;
  bound = kClockBound;
  while ((RCC->CR & RCC_CR_MSIRDY) == 0U && --bound != 0U) {
  }
  /* Let the oscillator settle at the lower range before removing the wait
   * state. */
  spin(200U);
  FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | saved.latency;
  bound = kClockBound;
  while ((FLASH->ACR & FLASH_ACR_LATENCY) != saved.latency && --bound != 0U) {
  }
}

/* ---- internal flash (tick-free equivalent of the HAL sequence) ------------ */

constexpr uint32_t kIntErrorFlags = FLASH_FLAG_SR_ERRORS;

bool intUnlock() {
  if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
  }
  if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
    return false;
  }
  /* Clear stale flags (OPTVERR is set on virgin parts). */
  FLASH->SR = kIntErrorFlags | FLASH_SR_EOP;
  return true;
}

void intLock() { FLASH->CR |= FLASH_CR_LOCK; }

bool intWaitIdle(uint32_t bound) {
  while ((FLASH->SR & (FLASH_SR_BSY | FLASH_SR_CFGBSY)) != 0U) {
    if (--bound == 0U) {
      return false;
    }
  }
  return true;
}

/* Wait for the operation just started, then report and clear its errors. */
bool intFinish(uint32_t bound) {
  while ((FLASH->SR & FLASH_SR_BSY) != 0U) {
    if (--bound == 0U) {
      return false;
    }
  }
  const uint32_t status = FLASH->SR;
  FLASH->SR = status & (kIntErrorFlags | FLASH_SR_EOP);
  if ((status & kIntErrorFlags & ~FLASH_SR_OPTVERR) != 0U) {
    return false;
  }
  while ((FLASH->SR & FLASH_SR_CFGBSY) != 0U) {
    if (--bound == 0U) {
      return false;
    }
  }
  return true;
}

/* `page` is the absolute flash page number. Refuses anything outside the
 * application slot. */
bool intErasePage(uint32_t page) {
  if (page < kFirstAppPage || page >= kFirstAppPage + kAppPageCount) {
    return false;
  }
  if (!intWaitIdle(kIntEraseBound) || (FLASH->SR & FLASH_SR_PESD) != 0U) {
    return false;
  }
  FLASH->SR = kIntErrorFlags | FLASH_SR_EOP;
  FLASH->CR = (FLASH->CR & ~FLASH_CR_PNB) | (page << FLASH_CR_PNB_Pos) |
              FLASH_CR_PER | FLASH_CR_STRT;
  const bool ok = intFinish(kIntEraseBound);
  FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PNB);
  return ok;
}

/* Refuses anything outside the application slot. */
bool intProgramDoubleword(uint32_t address, uint32_t low, uint32_t high) {
  if ((address & 7U) != 0U || address < ota::kAppStart ||
      address > ota::kAppEnd - 8U) {
    return false;
  }
  if (!intWaitIdle(kIntProgramBound) || (FLASH->SR & FLASH_SR_PESD) != 0U) {
    return false;
  }
  FLASH->SR = kIntErrorFlags | FLASH_SR_EOP;
  FLASH->CR |= FLASH_CR_PG;
  *reinterpret_cast<volatile uint32_t*>(address) = low;
  __ISB();
  *reinterpret_cast<volatile uint32_t*>(address + 4U) = high;
  const bool ok = intFinish(kIntProgramBound);
  FLASH->CR &= ~FLASH_CR_PG;
  if (!ok) {
    return false;
  }
  return *reinterpret_cast<volatile const uint32_t*>(address) == low &&
         *reinterpret_cast<volatile const uint32_t*>(address + 4U) == high;
}

/* ---- install -------------------------------------------------------------- */

/* Step 1: CRC-32 of the staged image, streamed through a small buffer, and
 * its first two vector-table words. */
uint32_t stagedImageCrc(uint32_t size, uint32_t* stack, uint32_t* reset) {
  uint8_t buffer[kChunk];
  uint32_t state = ota::kCrcInit;
  for (uint32_t offset = 0U; offset < size;) {
    const uint32_t count = (size - offset) < kChunk ? (size - offset) : kChunk;
    extRead(ota::kImageAddress + offset, buffer, count);
    if (offset == 0U) { /* size >= 8, so the first chunk holds both words */
      *stack = ota::readLe32(buffer);
      *reset = ota::readLe32(buffer + 4U);
    }
    state = ota::crc32Update(state, buffer, count);
    offset += count;
  }
  return ota::crc32Finish(state);
}

/* Copy image bytes [begin, end) from external to internal flash. `begin` is a
 * multiple of 8; a final partial doubleword is padded with 0xFF. */
bool programRange(uint32_t begin, uint32_t end) {
  uint8_t buffer[kChunk];
  for (uint32_t offset = begin; offset < end;) {
    const uint32_t count = (end - offset) < kChunk ? (end - offset) : kChunk;
    extRead(ota::kImageAddress + offset, buffer, count);
    const uint32_t padded = (count + 7U) & ~7UL;
    for (uint32_t i = count; i < padded; ++i) {
      buffer[i] = 0xFFU;
    }
    for (uint32_t i = 0U; i < padded; i += 8U) {
      const uint32_t low = ota::readLe32(buffer + i);
      const uint32_t high = ota::readLe32(buffer + i + 4U);
      if (low == 0xFFFFFFFFUL && high == 0xFFFFFFFFUL) {
        continue; /* already the erased value */
      }
      if (!intProgramDoubleword(ota::kAppStart + offset + i, low, high)) {
        return false;
      }
    }
    offset += count;
  }
  return true;
}

/* Step 2, one attempt: erase the pages the image covers, program, verify the
 * internal copy's CRC. `touched` is set once the application slot may have
 * been modified. */
bool installOnce(const ota::Header& header, bool& touched) {
  const uint32_t size = header.image_size;
  const uint32_t pages = (size + kPageSize - 1U) / kPageSize;
  if (size < ota::kMinImageSize || pages > kAppPageCount) {
    return false;
  }
  if (!intUnlock()) {
    return false;
  }

  bool ok = true;
  /* The first page (vector table) is erased first and programmed last, so
   * the slot never looks like a valid application while it is incomplete. */
  for (uint32_t page = 0U; ok && page < pages; ++page) {
    touched = true;
    ok = intErasePage(kFirstAppPage + page);
  }
  if (ok && size > kPageSize) {
    ok = programRange(kPageSize, size);
  }
  if (ok) {
    ok = programRange(0U, size < kPageSize ? size : kPageSize);
  }
  intLock();
  if (!ok) {
    return false;
  }

  /* Verify. A double ECC error while reading raises an NMI; the handler
   * counts it and returns, and it is treated as a mismatch here. */
  const uint32_t ecc_before = g_flash_ecc_faults;
  const uint32_t crc = ota::crc32(
      reinterpret_cast<const uint8_t*>(ota::kAppStart), size);
  return crc == header.image_crc32 && g_flash_ecc_faults == ecc_before;
}

/* After a failed install the slot holds a partial or corrupt image whose
 * vector table may still look valid. Make sure later resets land in USB DFU
 * instead of running it. */
void invalidateApplication() {
  if (!intUnlock()) {
    return;
  }
  (void)intErasePage(kFirstAppPage);
  const uint32_t stack =
      *reinterpret_cast<volatile const uint32_t*>(ota::kAppStart);
  const uint32_t reset =
      *reinterpret_cast<volatile const uint32_t*>(ota::kAppStart + 4U);
  if (ota::vectorTableIsValid(stack, reset)) {
    /* Programming all-zero over existing data is always permitted. */
    (void)intProgramDoubleword(ota::kAppStart, 0U, 0U);
  }
  intLock();
}

StagedUpdateOutcome install(const ota::Header& header) {
  uint32_t stack = 0U;
  uint32_t reset = 0U;
  if (stagedImageCrc(header.image_size, &stack, &reset) !=
      header.image_crc32) {
    (void)extWriteResult(ota::kResultStagedCrc);
    return StagedUpdateOutcome::kRejected;
  }
  if (!ota::vectorTableIsValid(stack, reset)) {
    (void)extWriteResult(ota::kResultBadImage);
    return StagedUpdateOutcome::kRejected;
  }

  /* The staged copy has just passed both checks: only now may the
   * application slot be erased. */
  bool touched = false;
  if (installOnce(header, touched) || installOnce(header, touched)) {
    (void)extWriteResult(ota::kResultInstalled);
    return StagedUpdateOutcome::kInstalled;
  }
  if (touched) {
    invalidateApplication();
  }
  (void)extWriteResult(ota::kResultProgramFailed);
  return StagedUpdateOutcome::kInstallFailed;
}

}  // namespace

StagedUpdateOutcome runStagedUpdate() {
  StagedUpdateOutcome outcome = StagedUpdateOutcome::kNone;
  PinState pins;
  extPinsAcquire(pins);

  if (extProbe()) {
    uint8_t raw[ota::kHeaderSize];
    extRead(ota::kHeaderAddress, raw, sizeof(raw));
    ota::Header header;
    if (ota::headerRequestsInstall(raw, &header)) {
      ClockState clock;
      clockRaise(clock);
      outcome = install(header);
      clockRestore(clock);
    }
  }

  extPinsRelease(pins);
  return outcome;
}
