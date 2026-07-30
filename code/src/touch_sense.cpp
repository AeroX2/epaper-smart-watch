#include "touch_sense.h"

#include "stm32wbxx_hal.h"

namespace {

TSC_HandleTypeDef tsc_handle;

}  // namespace

bool TouchSense::begin() {
  __HAL_RCC_TSC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {};
  gpio.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF9_TSC;
  HAL_GPIO_Init(GPIOB, &gpio);

  tsc_handle.Instance = TSC;
  tsc_handle.Init.CTPulseHighLength = TSC_CTPH_2CYCLES;
  tsc_handle.Init.CTPulseLowLength = TSC_CTPL_2CYCLES;
  tsc_handle.Init.SpreadSpectrum = DISABLE;
  tsc_handle.Init.SpreadSpectrumDeviation = 1;
  tsc_handle.Init.SpreadSpectrumPrescaler = TSC_SS_PRESC_DIV1;
  tsc_handle.Init.PulseGeneratorPrescaler = TSC_PG_PRESC_DIV4;
  tsc_handle.Init.MaxCountValue = TSC_MCV_8191;
  tsc_handle.Init.IODefaultMode = TSC_IODEF_OUT_PP_LOW;
  tsc_handle.Init.SynchroPinPolarity = TSC_SYNC_POLARITY_FALLING;
  tsc_handle.Init.AcquisitionMode = TSC_ACQ_MODE_NORMAL;

  initialized_ = HAL_TSC_Init(&tsc_handle) == HAL_OK;
  return initialized_;
}

bool TouchSense::acquire(uint32_t channel, uint16_t& value) {
  TSC_IOConfigTypeDef io = {};
  io.ChannelIOs = channel;
  io.SamplingIOs = TSC_GROUP1_IO1;  // PB12 and the external sampling capacitor.
  io.ShieldIOs = 0;

  if (HAL_TSC_IOConfig(&tsc_handle, &io) != HAL_OK) {
    return false;
  }

  HAL_TSC_IODischarge(&tsc_handle, ENABLE);
  delay(1);
  HAL_TSC_IODischarge(&tsc_handle, DISABLE);

  if (HAL_TSC_Start(&tsc_handle) != HAL_OK) {
    return false;
  }

  const uint32_t started = millis();
  while (HAL_TSC_GetState(&tsc_handle) == HAL_TSC_STATE_BUSY) {
    if (millis() - started > 20) {
      HAL_TSC_Stop(&tsc_handle);
      return false;
    }
  }

  const bool complete = HAL_TSC_GroupGetStatus(&tsc_handle, TSC_GROUP1_IDX) == TSC_GROUP_COMPLETED;
  value = static_cast<uint16_t>(HAL_TSC_GroupGetValue(&tsc_handle, TSC_GROUP1_IDX));
  HAL_TSC_Stop(&tsc_handle);
  return complete;
}

bool TouchSense::read(TouchReadings& readings) {
  return initialized_ && acquire(TSC_GROUP1_IO2, readings.channel_1) &&
         acquire(TSC_GROUP1_IO3, readings.channel_2) &&
         acquire(TSC_GROUP1_IO4, readings.channel_3);
}

bool TouchSense::ready() const {
  return initialized_;
}
