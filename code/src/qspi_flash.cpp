#include "qspi_flash.h"

#include "stm32wbxx_hal.h"

namespace {

QSPI_HandleTypeDef qspi_handle;

}  // namespace

extern "C" void HAL_QSPI_MspInit(QSPI_HandleTypeDef* handle) {
  if (handle->Instance != QUADSPI) {
    return;
  }

  __HAL_RCC_QUADSPI_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {};
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF10_QUADSPI;

  gpio.Pin = GPIO_PIN_3;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOD, &gpio);
}

extern "C" void HAL_QSPI_MspDeInit(QSPI_HandleTypeDef* handle) {
  if (handle->Instance != QUADSPI) {
    return;
  }
  HAL_GPIO_DeInit(GPIOA, GPIO_PIN_3);
  HAL_GPIO_DeInit(GPIOD, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
  __HAL_RCC_QUADSPI_CLK_DISABLE();
}

bool QspiFlash::begin() {
  qspi_handle.Instance = QUADSPI;
  qspi_handle.Init.ClockPrescaler = 7;  // 64 MHz / 8 = 8 MHz for conservative bring-up.
  qspi_handle.Init.FifoThreshold = 4;
  qspi_handle.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_NONE;
  qspi_handle.Init.FlashSize = 21;  // 2^(21 + 1) bytes = 4 MiB.
  qspi_handle.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
  qspi_handle.Init.ClockMode = QSPI_CLOCK_MODE_0;

  initialized_ = HAL_QSPI_Init(&qspi_handle) == HAL_OK;
  return initialized_;
}

bool QspiFlash::readIdentity(FlashIdentity& identity) {
  if (!initialized_) {
    return false;
  }

  QSPI_CommandTypeDef command = {};
  command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  command.Instruction = 0x9F;
  command.AddressMode = QSPI_ADDRESS_NONE;
  command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  command.DataMode = QSPI_DATA_1_LINE;
  command.DummyCycles = 0;
  command.NbData = 3;
  command.DdrMode = QSPI_DDR_MODE_DISABLE;
  command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

  uint8_t data[3] = {};
  if (HAL_QSPI_Command(&qspi_handle, &command, 100) != HAL_OK ||
      HAL_QSPI_Receive(&qspi_handle, data, 100) != HAL_OK) {
    return false;
  }

  identity.manufacturer = data[0];
  identity.memory_type = data[1];
  identity.capacity = data[2];
  return data[0] != 0x00 && data[0] != 0xFF && data[2] != 0x00 && data[2] != 0xFF;
}

bool QspiFlash::ready() const {
  return initialized_;
}
