/**
 * @file        main.h
 * @brief       Host stub standing in for the CubeMX generated main.h and the HAL.
 * @version     3.0.0
 *
 * @author      Nima Askari (NimaLTD)
 * @email       nima.askari@gmail.com
 * @github      https://www.github.com/nimaltd
 *
 * @copyright   (c) 2026 Nima Askari (NimaLTD)
 *              SPDX-License-Identifier: Apache-2.0
 *              See LICENSE.md in the project root for the full license text.
 *
 * @note        This file exists only so the library can be compiled and tested
 *              on a PC. It is not part of the shipped library, and it is never
 *              on the include path of a real STM32 build. The HAL functions
 *              here are implemented by the tests, over a model of the chip.
 */

#ifndef MAIN_H
#define MAIN_H

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include <stdint.h>

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* CMSIS spells volatile this way. */
#define __IO                    volatile

/* As the HAL defines it with USE_FULL_ASSERT set, so the tests see a NULL
   argument stopped. The tests define assert_failed(). */
#define assert_param(expr)      ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))

#define HAL_MAX_DELAY           0xFFFFFFFFU

#define HAL_SPI_ERROR_NONE      0x00000000U
#define HAL_SPI_ERROR_DMA       0x00000010U

#define GPIO_PIN_4              ((uint16_t)0x0010U)

/*
 * ****************************************************************************************************
 * Types
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief HAL status, with the HAL's own values.
 */
typedef enum
{
    HAL_OK      = 0x00U,
    HAL_ERROR   = 0x01U,
    HAL_BUSY    = 0x02U,
    HAL_TIMEOUT = 0x03U,

} HAL_StatusTypeDef;

/*****************************************************************************************************/
/**
 * @brief GPIO pin state, with the HAL's own values.
 */
typedef enum
{
    GPIO_PIN_RESET = 0U,
    GPIO_PIN_SET,

} GPIO_PinState;

/*****************************************************************************************************/
/**
 * @brief SPI state, with the HAL's own values.
 */
typedef enum
{
    HAL_SPI_STATE_RESET      = 0x00U,
    HAL_SPI_STATE_READY      = 0x01U,
    HAL_SPI_STATE_BUSY       = 0x02U,
    HAL_SPI_STATE_BUSY_TX    = 0x03U,
    HAL_SPI_STATE_BUSY_RX    = 0x04U,
    HAL_SPI_STATE_BUSY_TX_RX = 0x05U,
    HAL_SPI_STATE_ERROR      = 0x06U,
    HAL_SPI_STATE_ABORT      = 0x07U,

} HAL_SPI_StateTypeDef;

/*****************************************************************************************************/
/**
 * @brief A GPIO port. The tests only compare its address.
 */
typedef struct
{
    uint32_t ODR;

} GPIO_TypeDef;

/*****************************************************************************************************/
/**
 * @brief An SPI handle. The tests only compare its address.
 */
typedef struct
{
    uint32_t Instance;

} SPI_HandleTypeDef;

/*
 * ****************************************************************************************************
 * Public function prototypes
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Where a failed assert_param lands. The tests define it.
 */
void assert_failed(uint8_t *file, uint32_t line);

/*****************************************************************************************************/
/**
 * @brief Return the current tick. Backed by a clock the tests control.
 */
uint32_t HAL_GetTick(void);

/*****************************************************************************************************/
/**
 * @brief Wait, by moving the tests' clock on.
 */
void HAL_Delay(uint32_t Delay);

/*****************************************************************************************************/
/**
 * @brief Set a pin. The tests watch chip select through it.
 */
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);

/*****************************************************************************************************/
/**
 * @brief Send to the modelled chip.
 */
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size,
                                   uint32_t Timeout);

/*****************************************************************************************************/
/**
 * @brief Receive from the modelled chip.
 */
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size,
                                  uint32_t Timeout);

/*****************************************************************************************************/
/**
 * @brief Send to the modelled chip by DMA, finishing later on the tests' clock.
 */
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size);

/*****************************************************************************************************/
/**
 * @brief Receive from the modelled chip by DMA, finishing later on the tests' clock.
 */
HAL_StatusTypeDef HAL_SPI_Receive_DMA(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size);

/*****************************************************************************************************/
/**
 * @brief Whether a DMA transfer is still running.
 */
HAL_SPI_StateTypeDef HAL_SPI_GetState(SPI_HandleTypeDef *hspi);

/*****************************************************************************************************/
/**
 * @brief The error a DMA transfer ended with.
 */
uint32_t HAL_SPI_GetError(SPI_HandleTypeDef *hspi);

/*****************************************************************************************************/
/**
 * @brief Stop a DMA transfer.
 */
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *hspi);

#endif /* MAIN_H */
