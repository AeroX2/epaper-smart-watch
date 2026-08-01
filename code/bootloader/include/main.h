#pragma once

#include "stm32wbxx_hal.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_dfu.h"
#include "usbd_dfu_flash.h"

/* Arduino's HAL wrapper provides Error_Handler as a function-like macro.
 * CubeMX's generated USB sources use the ordinary function symbol. */
#ifdef Error_Handler
#undef Error_Handler
#endif

#ifdef __cplusplus
extern "C" {
#endif

void Error_Handler(void);

#ifdef __cplusplus
}
#endif
