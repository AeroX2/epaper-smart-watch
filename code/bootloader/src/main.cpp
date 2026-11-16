/*
 * STM32WB resident USB DFU bootloader for the e-paper smart watch.
 *
 * USB and flash support comes from ST's STM32CubeWB DFU_Standalone example.
 * The pre-init hook makes the application/DFU decision before the Arduino
 * runtime initializes peripherals or global objects.
 */
#include <Arduino.h>

extern "C" {
#include "main.h"
#include "usb_device.h"
}

#include "ota_format.h"
#include "staged_update.h"

namespace {

constexpr uint32_t APP_START_ADDRESS = ota::kAppStart;
constexpr uint32_t DFU_REQUEST_MAGIC = 0x31465544UL;

using ApplicationEntry = void (*)();

bool applicationIsValid() {
  const uint32_t stack = *reinterpret_cast<volatile const uint32_t*>(
      APP_START_ADDRESS);
  const uint32_t reset = *reinterpret_cast<volatile const uint32_t*>(
      APP_START_ADDRESS + 4U);
  return ota::vectorTableIsValid(stack, reset);
}

bool consumeDfuRequest() {
  if (RTC->BKP19R != DFU_REQUEST_MAGIC) {
    return false;
  }
  PWR->CR1 |= PWR_CR1_DBP;
  while ((PWR->CR1 & PWR_CR1_DBP) == 0U) {
  }
  RTC->BKP19R = 0U;
  __DSB();
  return true;
}

void configureSafeGpio() {
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOCEN |
                  RCC_AHB2ENR_GPIOEEN;
  (void)RCC->AHB2ENR;

  /* Drive display reset, LED/boost, vibration and buzzer low before changing
   * their modes. */
  GPIOA->BRR = GPIO_PIN_6 | GPIO_PIN_15;
  GPIOC->BRR = GPIO_PIN_3;
  GPIOE->BRR = GPIO_PIN_0;
  GPIOA->MODER = (GPIOA->MODER &
                  ~(GPIO_MODER_MODE6_Msk | GPIO_MODER_MODE15_Msk)) |
                 (1UL << GPIO_MODER_MODE6_Pos) |
                 (1UL << GPIO_MODER_MODE15_Pos);
  GPIOC->MODER = (GPIOC->MODER & ~GPIO_MODER_MODE3_Msk) |
                 (1UL << GPIO_MODER_MODE3_Pos);
  GPIOE->MODER = (GPIOE->MODER & ~GPIO_MODER_MODE0_Msk) |
                 (1UL << GPIO_MODER_MODE0_Pos);

  /* PCB button 1 / bottom-left: active-low recovery input. */
  GPIOC->MODER &= ~GPIO_MODER_MODE10_Msk;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~GPIO_PUPDR_PUPD10_Msk) |
                 (1UL << GPIO_PUPDR_PUPD10_Pos);
}

bool recoveryButtonPressed() {
  /* The pull-up was enabled a few instructions ago. Give the pin time to
   * charge through the ~40 k internal pull-up before sampling, otherwise a
   * released button can still read low and strand the watch in DFU. */
  for (volatile uint32_t settle = 0; settle < 2000U; settle = settle + 1U) {
  }
  return (GPIOC->IDR & GPIO_PIN_10) == 0U;
}

[[noreturn]] void jumpToApplication() {
  const uint32_t stack = *reinterpret_cast<volatile const uint32_t*>(
      APP_START_ADDRESS);
  const uint32_t reset = *reinterpret_cast<volatile const uint32_t*>(
      APP_START_ADDRESS + 4U);
  const auto entry = reinterpret_cast<ApplicationEntry>(reset);

  /* Reset_Handler expects reset-state interrupt masking. The application's
   * pre-init hook restores interrupts only after .data and .bss are valid. */
  __disable_irq();
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  SCB->VTOR = APP_START_ADDRESS;
  __DSB();
  __ISB();
  __set_MSP(stack);
  entry();
  while (true) {
  }
}

void bootDecision() {
  configureSafeGpio();
  /* 1. DFU request / recovery button: straight to USB DFU, and the external
   *    flash is not touched at all. */
  if (consumeDfuRequest() || recoveryButtonPressed()) {
    return;
  }
  /* 2. Staged update in external flash (code/EXTERNAL_FLASH.md). A failed
   *    install leaves no runnable application: stay in USB DFU. */
  if (runStagedUpdate() == StagedUpdateOutcome::kInstallFailed) {
    return;
  }
  /* 3. Normal boot: application if valid, else USB DFU. */
  if (applicationIsValid()) {
    jumpToApplication();
  }
}

using InitFunction = void (*)();
__attribute__((used, section(".preinit_array")))
InitFunction boot_decision_hook = bootDecision;

}  // namespace

void setup() {
  MX_USB_Device_Init();
}

void loop() {
  __WFI();
}

extern "C" void Error_Handler(void) {
  __disable_irq();
  while (true) {
    __WFI();
  }
}
