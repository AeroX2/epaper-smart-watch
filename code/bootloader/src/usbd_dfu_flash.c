/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           USB_Device/DFU_Standalone/USB_Device/App/usbd_dfu_flash.c
  * @author         MCD Application Team
  * @brief          Memory management layer
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2019-2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_dfu_flash.h"

#include <string.h>

/* USER CODE BEGIN INCLUDE */

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device.
  * @{
  */

/** @defgroup USBD_DFU
  * @brief Usb DFU device module.
  * @{
  */

/** @defgroup USBD_DFU_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_DFU_Private_Defines
  * @brief Private defines.
  * @{
  */

/* 186 x 4 KiB pages: application slot only. The resident bootloader and the
 * STM32WB wireless-stack reservation are intentionally not exposed. */
#define FLASH_DESC_STR      "@Watch Application /0x08010000/186*04Kg"

_Static_assert((((sizeof(FLASH_DESC_STR) - 1U) * 2U) + 2U) <=
                   USBD_MAX_STR_DESC_SIZ,
               "DFU memory-map descriptor exceeds USB string buffer");

/* USER CODE BEGIN PRIVATE_DEFINES */
#define FLASH_ERASE_TIME    (uint16_t)50
#define FLASH_PROGRAM_TIME  (uint16_t)50
#define WATCH_APP_START     0x08010000UL
#define WATCH_APP_END       0x080CA000UL
/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_DFU_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_DFU_Private_Variables
  * @brief Private variables.
  * @{
  */

/* USER CODE BEGIN PRIVATE_VARIABLES */

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_DFU_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_DFU_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static uint16_t FLASH_If_Init(void);
static uint16_t FLASH_If_Erase(uint32_t Add);
static uint16_t FLASH_If_Write(uint8_t *src, uint8_t *dest, uint32_t Len);
static uint8_t *FLASH_If_Read(uint8_t *src, uint8_t *dest, uint32_t Len);
static uint16_t FLASH_If_DeInit(void);
static uint16_t FLASH_If_GetStatus(uint32_t Add, uint8_t Cmd, uint8_t *buffer);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */
static uint32_t GetPage(uint32_t Address);
static uint8_t IsAppRange(uint32_t Address, uint32_t Length);
/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif
__ALIGN_BEGIN USBD_DFU_MediaTypeDef USBD_DFU_Flash_fops __ALIGN_END =
{
   (uint8_t*)FLASH_DESC_STR,
    FLASH_If_Init,
    FLASH_If_DeInit,
    FLASH_If_Erase,
    FLASH_If_Write,
    FLASH_If_Read,
    FLASH_If_GetStatus
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Memory initialization routine.
  * @retval USBD_OK if operation is successful, MAL_FAIL else.
  */
uint16_t FLASH_If_Init(void)
{
  /* USER CODE BEGIN 0 */
  /* Unlock the internal flash */
  return HAL_FLASH_Unlock() == HAL_OK ? USBD_OK : USBD_FAIL;
  /* USER CODE END 0 */
}

/**
  * @brief  De-Initializes Memory
  * @retval USBD_OK if operation is successful, MAL_FAIL else
  */
uint16_t FLASH_If_DeInit(void)
{
  /* USER CODE BEGIN 1 */
  /* Lock the internal flash */
  HAL_FLASH_Lock();

  return 0;
  /* USER CODE END 1 */
}

/**
  * @brief  Erase sector.
  * @param  Add: Address of sector to be erased.
  * @retval 0 if operation is successful, MAL_FAIL else.
  */
uint16_t FLASH_If_Erase(uint32_t Add)
{
  /* USER CODE BEGIN 2 */
  FLASH_EraseInitTypeDef eraseinitstruct;
  uint32_t PageError = 0U;
  HAL_StatusTypeDef status;

  if ((Add % FLASH_PAGE_SIZE) != 0U ||
      IsAppRange(Add, FLASH_PAGE_SIZE) == 0U)
  {
    return 1U;
  }

  /* Unlock the Flash to enable the flash control register access */
  HAL_FLASH_Unlock();

  /* Clear OPTVERR bit set on virgin samples */
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);

  /* Get the number of sector to erase from 1st sector */
  eraseinitstruct.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseinitstruct.Page = GetPage(Add);
  eraseinitstruct.NbPages = 1U;
  status = HAL_FLASHEx_Erase(&eraseinitstruct, &PageError);

  if (status != HAL_OK)
  {
    return 1U;
  }
  return 0U;
  /* USER CODE END 2 */
}

/**
  * @brief  Memory write routine.
  * @param  src: Pointer to the source buffer. Address to be written to.
  * @param  dest: Pointer to the destination buffer.
  * @param  Len: Number of data to be written (in bytes).
  * @retval USBD_OK if operation is successful, MAL_FAIL else.
  */
uint16_t FLASH_If_Write(uint8_t *src, uint8_t *dest, uint32_t Len)
{
  /* USER CODE BEGIN 3 */
  uint32_t i = 0U;
  uint32_t address = (uint32_t)dest;
  uint32_t programmed_length = (Len + 7U) & ~7U;

  if (src == NULL || Len == 0U || programmed_length < Len ||
      (address & 7U) != 0U ||
      IsAppRange(address, programmed_length) == 0U)
  {
    return 1U;
  }

  /* Clear OPTVERR bit set on virgin samples */
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);

  for (i = 0U; i < programmed_length; i += 8U)
  {
    uint64_t value = UINT64_MAX;
    uint32_t remaining = Len - i;
    uint32_t copy_length = remaining < 8U ? remaining : 8U;
    memcpy(&value, src + i, copy_length);

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + i,
                          value) == HAL_OK)
    {
      /* Check the written value */
      if (*(volatile const uint64_t *)(address + i) != value)
      {
        /* Flash content doesn't match SRAM content */
        return 2;
      }
    }
    else
    {
      /* Error occurred while writing data in Flash memory */
      return 1;
    }
  }
  return 0;
  /* USER CODE END 3 */
}

/**
  * @brief  Memory read routine.
  * @param  src: Pointer to the source buffer. Address to be written to.
  * @param  dest: Pointer to the destination buffer.
  * @param  Len: Number of data to be read (in bytes).
  * @retval Pointer to the physical address where data should be read.
  */
uint8_t *FLASH_If_Read(uint8_t *src, uint8_t *dest, uint32_t Len)
{
  /* Return a valid address to avoid HardFault */
  /* USER CODE BEGIN 4 */
  uint32_t i = 0;
  uint8_t *psrc = src;

  if (dest == NULL || IsAppRange((uint32_t)src, Len) == 0U)
  {
    return NULL;
  }

  for (i = 0; i < Len; i++)
  {
    dest[i] = *psrc++;
  }
  /* Return a valid address to avoid HardFault */
  return (uint8_t *) (dest);

  /* USER CODE END 4 */
}

/**
  * @brief  Get status routine
  * @param  Add: Address to be read from
  * @param  Cmd: Number of data to be read (in bytes)
  * @param  buffer: used for returning the time necessary for a program or an erase operation
  * @retval USBD_OK if operation is successful
  */
uint16_t FLASH_If_GetStatus(uint32_t Add, uint8_t Cmd, uint8_t *buffer)
{
  /* USER CODE BEGIN 5 */
  switch (Cmd)
  {
    case DFU_MEDIA_PROGRAM:
    buffer[1] = (uint8_t)FLASH_PROGRAM_TIME;
    buffer[2] = (uint8_t)(FLASH_PROGRAM_TIME << 8);
    buffer[3] = 0;
    break;

    case DFU_MEDIA_ERASE:
    default:
    buffer[1] = (uint8_t)FLASH_ERASE_TIME;
    buffer[2] = (uint8_t)(FLASH_ERASE_TIME << 8);
    buffer[3] = 0;
    break;
  }
  return (USBD_OK);
  /* USER CODE END 5 */
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */
/**
  * @brief  Gets the page of a given address
  * @param  Address: Address of the FLASH Memory
  * @retval The page of a given address
  */
static uint32_t GetPage(uint32_t Address)
{
  uint32_t page = 0U;

  if (Address < (FLASH_BASE + FLASH_BANK_SIZE))
  {
    /* Bank 1 */
    page = (Address - FLASH_BASE) / FLASH_PAGE_SIZE;
  }
  else
  {
    /* Bank 2 */
    page = (Address - (FLASH_BASE + FLASH_BANK_SIZE)) / FLASH_PAGE_SIZE;
  }

  return page;
}

static uint8_t IsAppRange(uint32_t Address, uint32_t Length)
{
  if (Length == 0U || Address < WATCH_APP_START || Address >= WATCH_APP_END)
  {
    return 0U;
  }

  return Length <= (WATCH_APP_END - Address) ? 1U : 0U;
}
/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */
