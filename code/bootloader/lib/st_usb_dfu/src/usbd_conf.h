/* Keep quoted includes inside ST's USB middleware on the bootloader's
 * CubeMX-generated configuration, ahead of Arduino's CDC configuration. */
#include "../../../include/usbd_conf.h"
