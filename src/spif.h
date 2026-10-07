/**
 * @file        spif.h
 * @brief       Driver for SPI NOR flash, W25Qxx and compatible, on the STM32 HAL.
 * @version     3.0.0
 *
 * @author      Nima Askari (NimaLTD)
 * @email       nima.askari@gmail.com
 * @github      https://www.github.com/nimaltd
 * @linkedin    https://www.linkedin.com/in/nimaltd
 * @youtube     https://www.youtube.com/@nimaltd
 * @instagram   https://instagram.com/github.nimaltd
 *
 * @copyright   (c) 2026 Nima Askari (NimaLTD)
 *              SPDX-License-Identifier: Apache-2.0
 *              See LICENSE.md in the project root for the full license text.
 */

#ifndef SPIF_H
#define SPIF_H

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "main.h"
#include "osal.h"
#include "spif_config.h"

/*
 * ****************************************************************************************************
 * Configuration checks
 * ****************************************************************************************************
*/

/* Checked here and never in spif_config.h. The part of that file between its
   USER CODE markers is the user's and survives every update, so a check in it
   could be edited away, and one added later would never reach it. */

/* The values SPIF_TRANSFER can take. They start at 1 on purpose: a misspelt
   name counts as 0 inside #if, and 0 is then refused below instead of quietly
   meaning one of them. */
#define SPIF_TRANSFER_POLLING   1
#define SPIF_TRANSFER_DMA       2

#ifndef SPIF_TRANSFER
#error "SPIF_TRANSFER is not defined. Add it to spif_config.h"
#elif (SPIF_TRANSFER != SPIF_TRANSFER_POLLING) && (SPIF_TRANSFER != SPIF_TRANSFER_DMA)
#error "SPIF_TRANSFER must be SPIF_TRANSFER_POLLING or SPIF_TRANSFER_DMA"
#endif

/* 0 is allowed here: it tries for the mutex once and does not wait. */
#ifndef SPIF_TIMEOUT_MUTEX_MS
#error "SPIF_TIMEOUT_MUTEX_MS is not defined. Add it to spif_config.h"
#endif

/* The others must allow some time, or every call would time out. */
#ifndef SPIF_TIMEOUT_TRANSFER_MS
#error "SPIF_TIMEOUT_TRANSFER_MS is not defined. Add it to spif_config.h"
#elif SPIF_TIMEOUT_TRANSFER_MS == 0
#error "SPIF_TIMEOUT_TRANSFER_MS must be greater than 0"
#endif

#ifndef SPIF_TIMEOUT_PAGE_MS
#error "SPIF_TIMEOUT_PAGE_MS is not defined. Add it to spif_config.h"
#elif SPIF_TIMEOUT_PAGE_MS == 0
#error "SPIF_TIMEOUT_PAGE_MS must be greater than 0"
#endif

#ifndef SPIF_TIMEOUT_SECTOR_MS
#error "SPIF_TIMEOUT_SECTOR_MS is not defined. Add it to spif_config.h"
#elif SPIF_TIMEOUT_SECTOR_MS == 0
#error "SPIF_TIMEOUT_SECTOR_MS must be greater than 0"
#endif

#ifndef SPIF_TIMEOUT_BLOCK32_MS
#error "SPIF_TIMEOUT_BLOCK32_MS is not defined. Add it to spif_config.h"
#elif SPIF_TIMEOUT_BLOCK32_MS == 0
#error "SPIF_TIMEOUT_BLOCK32_MS must be greater than 0"
#endif

#ifndef SPIF_TIMEOUT_BLOCK_MS
#error "SPIF_TIMEOUT_BLOCK_MS is not defined. Add it to spif_config.h"
#elif SPIF_TIMEOUT_BLOCK_MS == 0
#error "SPIF_TIMEOUT_BLOCK_MS must be greater than 0"
#endif

#ifndef SPIF_TIMEOUT_CHIP_PER_MB_MS
#error "SPIF_TIMEOUT_CHIP_PER_MB_MS is not defined. Add it to spif_config.h"
#elif SPIF_TIMEOUT_CHIP_PER_MB_MS == 0
#error "SPIF_TIMEOUT_CHIP_PER_MB_MS must be greater than 0"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* The same on every SPI NOR flash this driver supports. A sector, the smallest
   part a chip erases, is not: it is the sector_size field of spif_t. */
#define SPIF_PAGE_SIZE              256U    /* Most one write command can carry. */
#define SPIF_BLOCK32_SIZE           32768U
#define SPIF_BLOCK_SIZE             65536U

/* From an address to the number of the page, sector or block it falls in, and
   from a number back to the address it starts at. A sector's size depends on
   the chip, so the two sector ones take the handle as well. */
#define SPIF_ADDRESS_TO_PAGE(address)           ((uint32_t)(address) / SPIF_PAGE_SIZE)
#define SPIF_ADDRESS_TO_SECTOR(handle, address) ((uint32_t)(address) / (handle)->sector_size)
#define SPIF_ADDRESS_TO_BLOCK32(address)        ((uint32_t)(address) / SPIF_BLOCK32_SIZE)
#define SPIF_ADDRESS_TO_BLOCK(address)          ((uint32_t)(address) / SPIF_BLOCK_SIZE)
#define SPIF_PAGE_TO_ADDRESS(page)              ((uint32_t)(page) * SPIF_PAGE_SIZE)
#define SPIF_SECTOR_TO_ADDRESS(handle, sector)  ((uint32_t)(sector) * (handle)->sector_size)
#define SPIF_BLOCK32_TO_ADDRESS(block32)        ((uint32_t)(block32) * SPIF_BLOCK32_SIZE)
#define SPIF_BLOCK_TO_ADDRESS(block)            ((uint32_t)(block) * SPIF_BLOCK_SIZE)

/* Bytes spif_unique_id() reads. */
#define SPIF_UNIQUE_ID_SIZE         8U

/* JEDEC manufacturer IDs, as the manufacturer field of spif_t holds them. */
#define SPIF_MANUFACTURER_SPANSION   0x01U   /* also Cypress and Infineon */
#define SPIF_MANUFACTURER_FUJITSU    0x04U
#define SPIF_MANUFACTURER_EON        0x1CU
#define SPIF_MANUFACTURER_ATMEL      0x1FU   /* also Adesto and Renesas */
#define SPIF_MANUFACTURER_MICRON     0x20U   /* also ST and Numonyx */
#define SPIF_MANUFACTURER_XMC        0x20U   /* the same ID as Micron */
#define SPIF_MANUFACTURER_AMIC       0x37U
#define SPIF_MANUFACTURER_ZBIT       0x5EU
#define SPIF_MANUFACTURER_SANYO      0x62U
#define SPIF_MANUFACTURER_BOYA       0x68U
#define SPIF_MANUFACTURER_PUYA       0x85U
#define SPIF_MANUFACTURER_INTEL      0x89U
#define SPIF_MANUFACTURER_ESMT       0x8CU
#define SPIF_MANUFACTURER_ISSI       0x9DU
#define SPIF_MANUFACTURER_FUDAN      0xA1U
#define SPIF_MANUFACTURER_HYUNDAI    0xADU
#define SPIF_MANUFACTURER_SST        0xBFU   /* also Microchip */
#define SPIF_MANUFACTURER_MACRONIX   0xC2U
#define SPIF_MANUFACTURER_GIGADEVICE 0xC8U
#define SPIF_MANUFACTURER_WINBOND    0xEFU

/*
 * ****************************************************************************************************
 * Types
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Error values returned by every function.
 */
typedef enum
{
    SPIF_ERR_NONE      = 0, /**< Done, or for a job, started: see spif_wait().              */
    SPIF_ERR_INVALID   = 1, /**< A handle spif_init() did not accept, or not on this chip.   */
    SPIF_ERR_RANGE     = 2, /**< Past the end of the chip, or a number too high.             */
    SPIF_ERR_SPI       = 3, /**< The SPI transfer failed.                                    */
    SPIF_ERR_TIMEOUT   = 4, /**< Not finished in the time spif_config.h allows.              */
    SPIF_ERR_MUTEX     = 5, /**< The RTOS could not create or take the mutex.                */
    SPIF_ERR_CHIP      = 6, /**< No chip answered, or one this driver does not know.         */
    SPIF_ERR_PROTECTED = 7, /**< Write protected: the chip will not change some or all of it. */
    SPIF_ERR_BUSY      = 8, /**< A job is still running: see spif_is_busy().                */

} spif_err_t;

/*****************************************************************************************************/
/**
 * @brief One flash chip. Declare one per chip and hand it to spif_init().
 */
typedef struct
{
    SPI_HandleTypeDef *hspi;             /**< The bus the chip is on.                       */
    GPIO_TypeDef      *cs_port;          /**< Chip select port.                             */
    uint32_t          size;              /**< Bytes in the chip, 0 until spif_init() works. */
    uint32_t          sector_size;       /**< Bytes a sector erase clears, 4 KB on most.    */
    uint16_t          cs_pin;            /**< Chip select pin.                              */
    uint8_t           manufacturer;      /**< JEDEC maker, a SPIF_MANUFACTURER_ value.      */
    uint8_t           memory_type;       /**< JEDEC memory type, from the maker's own list. */
    uint8_t           family;            /**< Which of the makers' differences apply.       */
    uint8_t           address_bytes;     /**< Address bytes each command sends, 3 or 4.     */
    uint8_t           cmd_read;          /**< Fast read, for this chip's addressing.        */
    uint8_t           cmd_program;       /**< Page program.                                 */
    uint8_t           cmd_erase_sector;  /**< Sector erase.                                 */
    uint8_t           cmd_erase_block32; /**< 32 KB erase, 0 when the chip has none.        */
    uint8_t           cmd_erase_block;   /**< 64 KB erase, 0 when the chip has none.        */
    uint8_t           job_state;         /**< What the running job is doing.                */
    uint8_t           job_command;       /**< The command an erase job sends.               */
    spif_err_t        job_result;        /**< How the last job ended, for spif_wait().      */
    uint8_t           *job_data;         /**< The caller's buffer.                          */
    size_t            job_len;           /**< Bytes, or erases, in the whole job.           */
    size_t            job_done;          /**< Bytes, or erases, finished so far.            */
    uint32_t          job_address;       /**< Where in the chip the job starts.             */
    uint32_t          job_step;          /**< Bytes in the piece under way.                 */
    uint32_t          job_start;         /**< HAL_GetTick() when that piece started.        */
    uint32_t          job_timeout;       /**< Time that piece may take.                     */
    osal_mutex_t      mutex;             /**< Lets one thread use the chip at a time.       */

} spif_t;

/*
 * ****************************************************************************************************
 * Public function prototypes
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Set up a handle for one chip, find the chip and read its size.
 * @note  Call it once per handle. With an RTOS, only from a thread once the RTOS runs.
 */
spif_err_t spif_init(spif_t *handle, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port,
                     uint16_t cs_pin);

/*****************************************************************************************************/
/**
 * @brief Read len bytes starting at address. With DMA it is a job: it returns once started.
 */
spif_err_t spif_read(spif_t *handle, uint32_t address, uint8_t *data, size_t len);

/*****************************************************************************************************/
/**
 * @brief Start writing len bytes at address, into flash erased beforehand. Returns at once.
 */
spif_err_t spif_write(spif_t *handle, uint32_t address, const uint8_t *data, size_t len);

/*****************************************************************************************************/
/**
 * @brief Start erasing one sector, sector_size bytes, by its number. Returns at once.
 */
spif_err_t spif_erase_sector(spif_t *handle, uint32_t sector);

/*****************************************************************************************************/
/**
 * @brief Start erasing one 32 KB block, by its number. Returns at once.
 */
spif_err_t spif_erase_block32(spif_t *handle, uint32_t block32);

/*****************************************************************************************************/
/**
 * @brief Start erasing one 64 KB block, by its number. Returns at once.
 */
spif_err_t spif_erase_block(spif_t *handle, uint32_t block);

/*****************************************************************************************************/
/**
 * @brief Start erasing the whole chip, which can take minutes. Returns at once.
 */
spif_err_t spif_erase_chip(spif_t *handle);

/*****************************************************************************************************/
/**
 * @brief Read the chip's 64 bit unique ID, set at the factory.
 */
spif_err_t spif_unique_id(spif_t *handle, uint8_t *id);

/*****************************************************************************************************/
/**
 * @brief Whether a job is still running. Moves it on, and returns at once.
 */
bool spif_is_busy(spif_t *handle);

/*****************************************************************************************************/
/**
 * @brief Wait until the running job has ended, and say how it ended.
 */
spif_err_t spif_wait(spif_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* SPIF_H */
